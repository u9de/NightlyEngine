/* SPDX-FileCopyrightText: 2009 Blender Foundation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup edinterface
 *
 * Eyedropper (Color Band).
 *
 * Operates by either:
 * - Dragging a straight line, sampling pixels formed by the line to extract a gradient.
 * - Clicking on points, adding each color to the end of the color-band.
 *
 * Defines:
 * - #UI_OT_eyedropper_colorramp
 * - #UI_OT_eyedropper_colorramp_point
 */

#include "MEM_guardedalloc.h"

#include "DNA_screen_types.h"

#include "BLI_bitmap_draw_2d.h"
#include "BLI_math_vector.h"

#include "BKE_colorband.h"
#include "BKE_context.h"

#include "RNA_access.h"
#include "RNA_prototypes.h"

#include "UI_interface.h"

#include "WM_api.h"
#include "WM_types.h"

#include "interface_intern.hh"

#include "eyedropper_intern.hh"

struct Colorband_RNAUpdateCb {
  PointerRNA ptr;
  PropertyRNA *prop;
};

struct EyedropperColorband {
  int event_xy_last[2];
  /* Alpha is currently fixed at 1.0, may support in future. */
  float (*color_buffer)[4];
  int color_buffer_alloc;
  int color_buffer_len;
  bool sample_start;
  ColorBand init_color_band;
  ColorBand *color_band;
  PointerRNA ptr;
  PropertyRNA *prop;
  bool is_undo;
  bool is_set;
};

/* For user-data only. */
struct EyedropperColorband_Context {
  bContext *context;
  EyedropperColorband *eye;
};

static bool eyedropper_colorband_init(bContext *C, wmOperator *op)
{
  ColorBand *band = nullptr;

  uiBut *but = UI_context_active_but_get(C);

  PointerRNA rna_update_ptr = PointerRNA_NULL;
  PropertyRNA *rna_update_prop = nullptr;
  bool is_undo = true;

  if (but == nullptr) {
    /* pass */
  }
  else {
    if (but->type == UI_BTYPE_COLORBAND) {
      /* When invoked with a hotkey, we can find the band in 'but->poin'. */
      band = (ColorBand *)but->poin;
    }
    else {
      /* When invoked from a button it's in custom_data field. */
      band = (ColorBand *)but->custom_data;
    }

    if (band) {
      rna_update_ptr = ((Colorband_RNAUpdateCb *)but->func_argN)->ptr;
      rna_update_prop = ((Colorband_RNAUpdateCb *)but->func_argN)->prop;
      is_undo = UI_but_flag_is_set(but, UI_BUT_UNDO);
    }
  }

  if (!band) {
    const PointerRNA ptr = CTX_data_pointer_get_type(C, "color_ramp", &RNA_ColorRamp);
    if (ptr.data != nullptr) {
      band = static_cast<ColorBand *>(ptr.data);

      /* Set this to a sub-member of the property to trigger an update. */
      rna_update_ptr = ptr;
      rna_update_prop = &rna_ColorRamp_color_mode;
      is_undo = RNA_struct_undo_check(ptr.type);
    }
  }

  if (!band) {
    return false;
  }

  EyedropperColorband *eye = MEM_cnew<EyedropperColorband>(__func__);
  eye->color_buffer_alloc = 16;
  eye->color_buffer = static_cast<float(*)[4]>(
      MEM_mallocN(sizeof(*eye->color_buffer) * eye->color_buffer_alloc, __func__));
  eye->color_buffer_len = 0;
  eye->color_band = band;
  eye->init_color_band = *eye->color_band;
  eye->ptr = rna_update_ptr;
  eye->prop = rna_update_prop;
  eye->is_undo = is_undo;

  op->customdata = eye;

  return true;
}

static void eyedropper_colorband_sample_point(bContext *C,
                                              EyedropperColorband *eye,
                                              const int m_xy[2])
{
  if (eye->event_xy_last[0] != m_xy[0] || eye->event_xy_last[1] != m_xy[1]) {
    float col[4];
    col[3] = 1.0f; /* TODO: sample alpha */
    eyedropper_color_sample_fl(C, m_xy, col);
    if (eye->color_buffer_len + 1 == eye->color_buffer_alloc) {
      eye->color_buffer_alloc *= 2;
      eye->color_buffer = static_cast<float(*)[4]>(
          MEM_reallocN(eye->color_buffer, sizeof(*eye->color_buffer) * eye->color_buffer_alloc));
    }
    copy_v4_v4(eye->color_buffer[eye->color_buffer_len], col);
    eye->color_buffer_len += 1;
    copy_v2_v2_int(eye->event_xy_last, m_xy);
    eye->is_set = true;
  }
}

static bool eyedropper_colorband_sample_callback(int mx, int my, void *userdata)
{
  EyedropperColorband_Context *data = static_cast<EyedropperColorband_Context *>(userdata);
  bContext *C = data->context;
  EyedropperColorband *eye = data->eye;
  const int cursor[2] = {mx, my};
  eyedropper_colorband_sample_point(C, eye, cursor);
  return true;
}

static void eyedropper_colorband_sample_segment(bContext *C,
                                                EyedropperColorband *eye,
                                                const int m_xy[2])
{
  /* Since the mouse tends to move rather rapidly we use #BLI_bitmap_draw_2d_line_v2v2i
   * to interpolate between the reported coordinates */
  EyedropperColorband_Context userdata = {C, eye};
  BLI_bitmap_draw_2d_line_v2v2i(
      eye->event_xy_last, m_xy, eyedropper_colorband_sample_callback, &userdata);
}

static void eyedropper_colorband_exit(bContext *C, wmOperator *op)
{
  WM_cursor_modal_restore(CTX_wm_window(C));

  if (op->customdata) {
    EyedropperColorband *eye = static_cast<EyedropperColorband *>(op->customdata);
    MEM_freeN(eye->color_buffer);
    MEM_freeN(eye);
    op->customdata = nullptr;
  }
}

static void eyedropper_colorband_apply(bContext *C, wmOperator *op)
{
  EyedropperColorband *eye = static_cast<EyedropperColorband *>(op->customdata);
  /* Always filter, avoids noise in resulting color-band. */
  const bool filter_samples = true;
  BKE_colorband_init_from_table_rgba(
      eye->color_band, eye->color_buffer, eye->color_buffer_len, filter_samples);
  eye->is_set = true;
  if (eye->prop) {
    RNA_property_update(C, &eye->ptr, eye->prop);
  }
}

static void eyedropper_colorband_cancel(bContext *C, wmOperator *op)
{
  EyedropperColorband *eye = static_cast<EyedropperColorband *>(op->customdata);
  if (eye->is_set) {
    *eye->color_band = eye->init_color_band;
    if (eye->prop) {
      RNA_property_update(C, &eye->ptr, eye->prop);
    }
  }
  eyedropper_colorband_exit(C, op);
}

/* main modal status check */
static int eyedropper_colorband_modal(bContext *C, wmOperator *op, const wmEvent *event)
{
  EyedropperColorband *eye = static_cast<EyedropperColorband *>(op->customdata);
  /* handle modal keymap */
  if (event->type == EVT_MODAL_MAP) {
    switch (event->val) {
      case EYE_MODAL_CANCEL:
        eyedropper_colorband_cancel(C, op);
        return OPERATOR_CANCELLED;
      case EYE_MODAL_SAMPLE_CONFIRM: {
        const bool is_undo = eye->is_undo;
        eyedropper_colorband_sample_segment(C, eye, event->xy);
        eyedropper_colorband_apply(C, op);
        eyedropper_colorband_exit(C, op);
        /* Could support finished & undo-skip. */
        return is_undo ? OPERATOR_FINISHED : OPERATOR_CANCELLED;
      }
      case EYE_MODAL_SAMPLE_BEGIN:
        /* enable accum and make first sample */
        eye->sample_start = true;
        eyedropper_colorband_sample_point(C, eye, event->xy);
        eyedropper_colorband_apply(C, op);
        copy_v2_v2_int(eye->event_xy_last, event->xy);
        break;
      case EYE_MODAL_SAMPLE_RESET:
        break;
    }
  }
  else if (event->type == MOUSEMOVE) {
    if (eye->sample_start) {
      eyedropper_colorband_sample_segment(C, eye, event->xy);
      eyedropper_colorband_apply(C, op);
    }
  }
  return OPERATOR_RUNNING_MODAL;
}

static int eyedropper_colorband_point_modal(bContext *C, wmOperator *op, const wmEvent *event)
{
  EyedropperColorband *eye = static_cast<EyedropperColorband *>(op->customdata);
  /* handle modal keymap */
  if (event->type == EVT_MODAL_MAP) {
    switch (event->val) {
      case EYE_MODAL_POINT_CANCEL:
        eyedropper_colorband_cancel(C, op);
        return OPERATOR_CANCELLED;
      case EYE_MODAL_POINT_CONFIRM:
        eyedropper_colorband_apply(C, op);
        eyedropper_colorband_exit(C, op);
        return OPERATOR_FINISHED;
      case EYE_MODAL_POINT_REMOVE_LAST:
        if (eye->color_buffer_len > 0) {
          eye->color_buffer_len -= 1;
          eyedropper_colorband_apply(C, op);
        }
        break;
      case EYE_MODAL_POINT_SAMPLE:
        eyedropper_colorband_sample_point(C, eye, event->xy);
        eyedropper_colorband_apply(C, op);
        if (eye->color_buffer_len == MAXCOLORBAND) {
          eyedropper_colorband_exit(C, op);
          return OPERATOR_FINISHED;
        }
        break;
      case EYE_MODAL_SAMPLE_RESET:
        *eye->color_band = eye->init_color_band;
        if (eye->prop) {
          RNA_property_update(C, &eye->ptr, eye->prop);
        }
        eye->color_buffer_len = 0;
        break;
    }
  }
  return OPERATOR_RUNNING_MODAL;
}

/* Modal Operator init */
static int eyedropper_colorband_invoke(bContext *C, wmOperator *op, const wmEvent * /*event*/)
{
  /* init */
  if (eyedropper_colorband_init(C, op)) {
    wmWindow *win = CTX_wm_window(C);
    /* Workaround for de-activating the button clearing the cursor, see #76794 */
    UI_context_active_but_clear(C, win, CTX_wm_region(C));
    WM_cursor_modal_set(win, WM_CURSOR_EYEDROPPER);

    /* add temp handler */
    WM_event_add_modal_handler(C, op);

    return OPERATOR_RUNNING_MODAL;
  }
  return OPERATOR_CANCELLED;
}

/* Repeat operator */
static int eyedropper_colorband_exec(bContext *C, wmOperator *op)
{
  /* init */
  if (eyedropper_colorband_init(C, op)) {

    /* do something */

    /* cleanup */
    eyedropper_colorband_exit(C, op);

    return OPERATOR_FINISHED;
  }
  return OPERATOR_CANCELLED;
}

static bool eyedropper_colorband_poll(bContext *C)
{
  uiBut *but = UI_context_active_but_get(C);
  if (but && but->type == UI_BTYPE_COLORBAND) {
    return true;
  }
  const PointerRNA ptr = CTX_data_pointer_get_type(C, "color_ramp", &RNA_ColorRamp);
  if (ptr.data != nullptr) {
    return true;
  }
  return false;
}

void UI_OT_eyedropper_colorramp(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Eyedropper Colorband";
  ot->idname = "UI_OT_eyedropper_colorramp";
  ot->description = "Sample a color band";

  /* api callbacks */
  ot->invoke = eyedropper_colorband_invoke;
  ot->modal = eyedropper_colorband_modal;
  ot->cancel = eyedropper_colorband_cancel;
  ot->exec = eyedropper_colorband_exec;
  ot->poll = eyedropper_colorband_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO | OPTYPE_BLOCKING | OPTYPE_INTERNAL;

  /* properties */
}

void UI_OT_eyedropper_colorramp_point(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Eyedropper Colorband (Points)";
  ot->idname = "UI_OT_eyedropper_colorramp_point";
  ot->description = "Point-sample a color band";

  /* api callbacks */
  ot->invoke = eyedropper_colorband_invoke;
  ot->modal = eyedropper_colorband_point_modal;
  ot->cancel = eyedropper_colorband_cancel;
  ot->exec = eyedropper_colorband_exec;
  ot->poll = eyedropper_colorband_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO | OPTYPE_BLOCKING | OPTYPE_INTERNAL;

  /* properties */
}
