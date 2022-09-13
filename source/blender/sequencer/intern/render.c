/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2001-2002 NaN Holding BV. All rights reserved.
 *           2003-2009 Blender Foundation.
 *           2005-2006 Peter Schlaile <peter [at] schlaile [dot] de> */

/** \file
 * \ingroup bke
 */

#include <time.h>

#include "MEM_guardedalloc.h"

#include "DNA_anim_types.h"
#include "DNA_defaults.h"
#include "DNA_mask_types.h"
#include "DNA_scene_types.h"
#include "DNA_sequence_types.h"
#include "DNA_space_types.h"

#include "BLI_linklist.h"
#include "BLI_listbase.h"
#include "BLI_path_util.h"
#include "BLI_rect.h"

#include "BKE_anim_data.h"
#include "BKE_animsys.h"
#include "BKE_fcurve.h"
#include "BKE_global.h"
#include "BKE_image.h"
#include "BKE_layer.h"
#include "BKE_lib_id.h"
#include "BKE_main.h"
#include "BKE_mask.h"
#include "BKE_movieclip.h"
#include "BKE_scene.h"
#include "BKE_sequencer_offscreen.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "IMB_colormanagement.h"
#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"
#include "IMB_metadata.h"

#include "RNA_access.h"
#include "RNA_prototypes.h"

#include "RE_engine.h"
#include "RE_pipeline.h"

#include "SEQ_channels.h"
#include "SEQ_effects.h"
#include "SEQ_iterator.h"
#include "SEQ_modifier.h"
#include "SEQ_proxy.h"
#include "SEQ_relations.h"
#include "SEQ_render.h"
#include "SEQ_sequencer.h"
#include "SEQ_time.h"
#include "SEQ_transform.h"
#include "SEQ_utils.h"

#include "effects.h"
#include "image_cache.h"
#include "multiview.h"
#include "prefetch.h"
#include "proxy.h"
#include "render.h"
#include "strip_time.h"
#include "utils.h"

static ImBuf *seq_render_strip_stack(const SeqRenderData *context,
                                     SeqRenderState *state,
                                     ListBase *channels,
                                     ListBase *seqbasep,
                                     float timeline_frame,
                                     int chanshown);

static ThreadMutex seq_render_mutex = BLI_MUTEX_INITIALIZER;
SequencerDrawView sequencer_view3d_fn = NULL; /* NULL in background mode */

/* -------------------------------------------------------------------- */
/** \name Color-space utility functions
 * \{ */

void seq_imbuf_assign_spaces(Scene *scene, ImBuf *ibuf)
{
#if 0
  /* Bute buffer is supposed to be in sequencer working space already. */
  if (ibuf->rect != NULL) {
    IMB_colormanagement_assign_rect_colorspace(ibuf, scene->sequencer_colorspace_settings.name);
  }
#endif
  if (ibuf->rect_float != NULL) {
    IMB_colormanagement_assign_float_colorspace(ibuf, scene->sequencer_colorspace_settings.name);
  }
}

void seq_imbuf_to_sequencer_space(Scene *scene, ImBuf *ibuf, bool make_float)
{
  /* Early output check: if both buffers are NULL we have nothing to convert. */
  if (ibuf->rect_float == NULL && ibuf->rect == NULL) {
    return;
  }
  /* Get common conversion settings. */
  const char *to_colorspace = scene->sequencer_colorspace_settings.name;
  /* Perform actual conversion logic. */
  if (ibuf->rect_float == NULL) {
    /* We are not requested to give float buffer and byte buffer is already
     * in thee required colorspace. Can skip doing anything here.
     */
    const char *from_colorspace = IMB_colormanagement_get_rect_colorspace(ibuf);
    if (!make_float && STREQ(from_colorspace, to_colorspace)) {
      return;
    }
    if (false) {
      /* The idea here is to provide as fast playback as possible and
       * enforcing float buffer here (a) uses more cache memory (b) might
       * make some other effects slower to apply.
       *
       * However, this might also have negative effect by adding weird
       * artifacts which will then not happen in final render.
       */
      IMB_colormanagement_transform_byte_threaded((unsigned char *)ibuf->rect,
                                                  ibuf->x,
                                                  ibuf->y,
                                                  ibuf->channels,
                                                  from_colorspace,
                                                  to_colorspace);
    }
    else {
      /* We perform conversion to a float buffer so we don't worry about
       * precision loss.
       */
      imb_addrectfloatImBuf(ibuf, 4);
      IMB_colormanagement_transform_from_byte_threaded(ibuf->rect_float,
                                                       (unsigned char *)ibuf->rect,
                                                       ibuf->x,
                                                       ibuf->y,
                                                       ibuf->channels,
                                                       from_colorspace,
                                                       to_colorspace);
      /* We don't need byte buffer anymore. */
      imb_freerectImBuf(ibuf);
    }
  }
  else {
    const char *from_colorspace = IMB_colormanagement_get_float_colorspace(ibuf);
    /* Unknown input color space, can't perform conversion. */
    if (from_colorspace == NULL || from_colorspace[0] == '\0') {
      return;
    }
    /* We don't want both byte and float buffers around: they'll either run
     * out of sync or conversion of byte buffer will lose precision in there.
     */
    if (ibuf->rect != NULL) {
      imb_freerectImBuf(ibuf);
    }
    IMB_colormanagement_transform_threaded(
        ibuf->rect_float, ibuf->x, ibuf->y, ibuf->channels, from_colorspace, to_colorspace, true);
  }
  seq_imbuf_assign_spaces(scene, ibuf);
}

void SEQ_render_imbuf_from_sequencer_space(Scene *scene, ImBuf *ibuf)
{
  const char *from_colorspace = scene->sequencer_colorspace_settings.name;
  const char *to_colorspace = IMB_colormanagement_role_colorspace_name_get(
      COLOR_ROLE_SCENE_LINEAR);

  if (!ibuf->rect_float) {
    return;
  }

  if (to_colorspace && to_colorspace[0] != '\0') {
    IMB_colormanagement_transform_threaded(
        ibuf->rect_float, ibuf->x, ibuf->y, ibuf->channels, from_colorspace, to_colorspace, true);
    IMB_colormanagement_assign_float_colorspace(ibuf, to_colorspace);
  }
}

void SEQ_render_pixel_from_sequencer_space_v4(struct Scene *scene, float pixel[4])
{
  const char *from_colorspace = scene->sequencer_colorspace_settings.name;
  const char *to_colorspace = IMB_colormanagement_role_colorspace_name_get(
      COLOR_ROLE_SCENE_LINEAR);

  if (to_colorspace && to_colorspace[0] != '\0') {
    IMB_colormanagement_transform_v4(pixel, from_colorspace, to_colorspace);
  }
  else {
    /* if no color management enables fallback to legacy conversion */
    srgb_to_linearrgb_v4(pixel, pixel);
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Rendering utility functions
 * \{ */

void SEQ_render_new_render_data(Main *bmain,
                                struct Depsgraph *depsgraph,
                                Scene *scene,
                                int rectx,
                                int recty,
                                int preview_render_size,
                                int for_render,
                                SeqRenderData *r_context)
{
  r_context->bmain = bmain;
  r_context->depsgraph = depsgraph;
  r_context->scene = scene;
  r_context->rectx = rectx;
  r_context->recty = recty;
  r_context->preview_render_size = preview_render_size;
  r_context->for_render = for_render;
  r_context->motion_blur_samples = 0;
  r_context->motion_blur_shutter = 0;
  r_context->skip_cache = false;
  r_context->is_proxy_render = false;
  r_context->view_id = 0;
  r_context->gpu_offscreen = NULL;
  r_context->task_id = SEQ_TASK_MAIN_RENDER;
  r_context->is_prefetch_render = false;
}

void seq_render_state_init(SeqRenderState *state)
{
  state->scene_parents = NULL;
}

StripElem *SEQ_render_give_stripelem(const Scene *scene, Sequence *seq, int timeline_frame)
{
  StripElem *se = seq->strip->stripdata;

  if (seq->type == SEQ_TYPE_IMAGE) {
    /* only IMAGE strips use the whole array, MOVIE strips use only the first element,
     * all other strips don't use this...
     */

    int frame_index = (int)seq_give_frame_index(scene, seq, timeline_frame);

    if (frame_index == -1 || se == NULL) {
      return NULL;
    }

    se += frame_index + seq->anim_startofs;
  }
  return se;
}

static int seq_channel_cmp_fn(const void *a, const void *b)
{
  return (*(Sequence **)a)->machine - (*(Sequence **)b)->machine;
}

int seq_get_shown_sequences(const Scene *scene,
                            ListBase *channels,
                            ListBase *seqbase,
                            const int timeline_frame,
                            const int chanshown,
                            Sequence **r_seq_arr)
{
  SeqCollection *collection = SEQ_query_rendered_strips(
      scene, channels, seqbase, timeline_frame, chanshown);
  const int strip_count = BLI_gset_len(collection->set);

  if (strip_count > MAXSEQ) {
    BLI_assert_msg(0, "Too many strips, this shouldn't happen");
    return 0;
  }

  /* Copy collection elements into array. */
  memset(r_seq_arr, 0, sizeof(Sequence *) * (MAXSEQ + 1));
  Sequence *seq;
  int index = 0;
  SEQ_ITERATOR_FOREACH (seq, collection) {
    r_seq_arr[index] = seq;
    index++;
  }
  SEQ_collection_free(collection);

  /* Sort array by channel. */
  qsort(r_seq_arr, strip_count, sizeof(Sequence *), seq_channel_cmp_fn);

  return strip_count;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Preprocessing & Effects
 *
 * Input preprocessing for SEQ_TYPE_IMAGE, SEQ_TYPE_MOVIE, SEQ_TYPE_MOVIECLIP and SEQ_TYPE_SCENE.
 *
 * Do all the things you can't really do afterwards using sequence effects
 * (read: before re-scaling to render resolution has been done).
 *
 * Order is important!
 *
 * - De-interlace.
 * - Crop and transform in image source coordinate space.
 * - Flip X + Flip Y (could be done afterwards, backward compatibility).
 * - Promote image to float data (affects pipeline operations afterwards).
 * - Color balance (is most efficient in the byte -> float
 *   (future: half -> float should also work fine!)
 *   case, if done on load, since we can use lookup tables).
 * - Pre-multiply.
 * \{ */

static bool sequencer_use_transform(const Sequence *seq)
{
  const StripTransform *transform = seq->strip->transform;

  if (transform->xofs != 0 || transform->yofs != 0 || transform->scale_x != 1 ||
      transform->scale_y != 1 || transform->rotation != 0) {
    return true;
  }

  return false;
}

static bool sequencer_use_crop(const Sequence *seq)
{
  const StripCrop *crop = seq->strip->crop;
  if (crop->left > 0 || crop->right > 0 || crop->top > 0 || crop->bottom > 0) {
    return true;
  }

  return false;
}

static bool seq_input_have_to_preprocess(const SeqRenderData *context,
                                         Sequence *seq,
                                         float UNUSED(timeline_frame))
{
  float mul;

  if (context && context->is_proxy_render) {
    return false;
  }

  if ((seq->flag & (SEQ_FILTERY | SEQ_FLIPX | SEQ_FLIPY | SEQ_MAKE_FLOAT)) ||
      sequencer_use_crop(seq) || sequencer_use_transform(seq)) {
    return true;
  }

  mul = seq->mul;

  if (seq->blend_mode == SEQ_BLEND_REPLACE) {
    mul *= seq->blend_opacity / 100.0f;
  }

  if (mul != 1.0f) {
    return true;
  }

  if (seq->sat != 1.0f) {
    return true;
  }

  if (seq->modifiers.first) {
    return true;
  }

  return false;
}

/**
 * Effect, mask and scene in strip input strips are rendered in preview resolution.
 * They are already down-scaled. #input_preprocess() does not expect this to happen.
 * Other strip types are rendered with original media resolution, unless proxies are
 * enabled for them. With proxies `is_proxy_image` will be set correctly to true.
 */
static bool seq_need_scale_to_render_size(const Sequence *seq, bool is_proxy_image)
{
  if (is_proxy_image) {
    return true;
  }
  if ((seq->type & SEQ_TYPE_EFFECT) != 0 || seq->type == SEQ_TYPE_MASK ||
      seq->type == SEQ_TYPE_META ||
      (seq->type == SEQ_TYPE_SCENE && ((seq->flag & SEQ_SCENE_STRIPS) != 0))) {
    return true;
  }
  return false;
}

static void sequencer_image_crop_transform_matrix(const Sequence *seq,
                                                  const ImBuf *in,
                                                  const ImBuf *out,
                                                  const float image_scale_factor,
                                                  const float preview_scale_factor,
                                                  float r_transform_matrix[4][4])
{
  const StripTransform *transform = seq->strip->transform;
  const float scale_x = transform->scale_x * image_scale_factor;
  const float scale_y = transform->scale_y * image_scale_factor;
  const float image_center_offs_x = (out->x - in->x) / 2;
  const float image_center_offs_y = (out->y - in->y) / 2;
  const float translate_x = transform->xofs * preview_scale_factor + image_center_offs_x;
  const float translate_y = transform->yofs * preview_scale_factor + image_center_offs_y;
  const float pivot[3] = {in->x * transform->origin[0], in->y * transform->origin[1], 0.0f};

  float rotation_matrix[3][3];
  axis_angle_to_mat3_single(rotation_matrix, 'Z', transform->rotation);
  loc_rot_size_to_mat4(r_transform_matrix,
                       (const float[]){translate_x, translate_y, 0.0f},
                       rotation_matrix,
                       (const float[]){scale_x, scale_y, 1.0f});
  transform_pivot_set_m4(r_transform_matrix, pivot);
  invert_m4(r_transform_matrix);
}

static void sequencer_image_crop_init(const Sequence *seq,
                                      const ImBuf *in,
                                      float crop_scale_factor,
                                      rctf *r_crop)
{
  const StripCrop *c = seq->strip->crop;
  const int left = c->left * crop_scale_factor;
  const int right = c->right * crop_scale_factor;
  const int top = c->top * crop_scale_factor;
  const int bottom = c->bottom * crop_scale_factor;

  BLI_rctf_init(r_crop, left, in->x - right, bottom, in->y - top);
}

static void sequencer_thumbnail_transform(ImBuf *in, ImBuf *out)
{
  float image_scale_factor = (float)out->x / in->x;
  float transform_matrix[4][4];

  /* Set to keep same loc,scale,rot but change scale to thumb size limit. */
  const float scale_x = 1 * image_scale_factor;
  const float scale_y = 1 * image_scale_factor;
  const float image_center_offs_x = (out->x - in->x) / 2;
  const float image_center_offs_y = (out->y - in->y) / 2;
  const float pivot[3] = {in->x / 2, in->y / 2, 0.0f};

  float rotation_matrix[3][3];
  unit_m3(rotation_matrix);
  loc_rot_size_to_mat4(transform_matrix,
                       (const float[]){image_center_offs_x, image_center_offs_y, 0.0f},
                       rotation_matrix,
                       (const float[]){scale_x, scale_y, 1.0f});
  transform_pivot_set_m4(transform_matrix, pivot);
  invert_m4(transform_matrix);

  IMB_transform(in, out, IMB_TRANSFORM_MODE_REGULAR, IMB_FILTER_NEAREST, transform_matrix, NULL);
}

/* Check whether transform introduces transparent ares in the result (happens when the transformed
 * image does not fully cover the render frame).
 *
 * The check is done by checking whether all corners of viewport fit inside of the transformed
 * image. If they do not the image will have transparent areas. */
static bool seq_image_transform_transparency_gained(const SeqRenderData *context, Sequence *seq)
{
  Scene *scene = context->scene;
  const int x = context->rectx;
  const int y = context->recty;

  float seq_image_quad[4][2];
  SEQ_image_transform_final_quad_get(scene, seq, seq_image_quad);
  for (int i = 0; i < 4; i++) {
    add_v2_v2(seq_image_quad[i], (float[]){x / 2, y / 2});
  }

  return !isect_point_quad_v2((float[]){x, y},
                              seq_image_quad[0],
                              seq_image_quad[1],
                              seq_image_quad[2],
                              seq_image_quad[3]) ||
         !isect_point_quad_v2((float[]){0, y},
                              seq_image_quad[0],
                              seq_image_quad[1],
                              seq_image_quad[2],
                              seq_image_quad[3]) ||
         !isect_point_quad_v2((float[]){x, 0},
                              seq_image_quad[0],
                              seq_image_quad[1],
                              seq_image_quad[2],
                              seq_image_quad[3]) ||
         !isect_point_quad_v2((float[]){0, 0},
                              seq_image_quad[0],
                              seq_image_quad[1],
                              seq_image_quad[2],
                              seq_image_quad[3]);
}

static void sequencer_preprocess_transform_crop(
    ImBuf *in, ImBuf *out, const SeqRenderData *context, Sequence *seq, const bool is_proxy_image)
{
  const Scene *scene = context->scene;
  const float preview_scale_factor = context->preview_render_size == SEQ_RENDER_SIZE_SCENE ?
                                         (float)scene->r.size / 100 :
                                         SEQ_rendersize_to_scale_factor(
                                             context->preview_render_size);
  const bool do_scale_to_render_size = seq_need_scale_to_render_size(seq, is_proxy_image);
  const float image_scale_factor = do_scale_to_render_size ? 1.0f : preview_scale_factor;

  float transform_matrix[4][4];
  sequencer_image_crop_transform_matrix(
      seq, in, out, image_scale_factor, preview_scale_factor, transform_matrix);

  /* Proxy image is smaller, so crop values must be corrected by proxy scale factor.
   * Proxy scale factor always matches preview_scale_factor. */
  rctf source_crop;
  const float crop_scale_factor = do_scale_to_render_size ? preview_scale_factor : 1.0f;
  sequencer_image_crop_init(seq, in, crop_scale_factor, &source_crop);

  eIMBInterpolationFilterMode filter;
  const StripTransform *transform = seq->strip->transform;
  if (transform->filter == SEQ_TRANSFORM_FILTER_NEAREST) {
    filter = IMB_FILTER_NEAREST;
  }
  else {
    filter = IMB_FILTER_BILINEAR;
  }

  IMB_transform(in, out, IMB_TRANSFORM_MODE_CROP_SRC, filter, transform_matrix, &source_crop);

  if (!seq_image_transform_transparency_gained(context, seq)) {
    out->planes = in->planes;
  }
  else {
    out->planes = R_IMF_PLANES_RGBA;
  }
}

static void multibuf(ImBuf *ibuf, const float fmul)
{
  char *rt;
  float *rt_float;

  int a;

  rt = (char *)ibuf->rect;
  rt_float = ibuf->rect_float;

  if (rt) {
    const int imul = (int)(256.0f * fmul);
    a = ibuf->x * ibuf->y;
    while (a--) {
      rt[0] = min_ii((imul * rt[0]) >> 8, 255);
      rt[1] = min_ii((imul * rt[1]) >> 8, 255);
      rt[2] = min_ii((imul * rt[2]) >> 8, 255);
      rt[3] = min_ii((imul * rt[3]) >> 8, 255);

      rt += 4;
    }
  }
  if (rt_float) {
    a = ibuf->x * ibuf->y;
    while (a--) {
      rt_float[0] *= fmul;
      rt_float[1] *= fmul;
      rt_float[2] *= fmul;
      rt_float[3] *= fmul;

      rt_float += 4;
    }
  }

  if (ELEM(ibuf->planes, R_IMF_PLANES_BW, R_IMF_PLANES_RGB) && fmul < 1.0f) {
    ibuf->planes = R_IMF_PLANES_RGBA;
  }
}

static ImBuf *input_preprocess(const SeqRenderData *context,
                               Sequence *seq,
                               float timeline_frame,
                               ImBuf *ibuf,
                               const bool is_proxy_image)
{
  Scene *scene = context->scene;
  ImBuf *preprocessed_ibuf = NULL;

  /* Deinterlace. */
  if ((seq->flag & SEQ_FILTERY) && !ELEM(seq->type, SEQ_TYPE_MOVIE, SEQ_TYPE_MOVIECLIP)) {
    /* Change original image pointer to avoid another duplication in SEQ_USE_TRANSFORM. */
    preprocessed_ibuf = IMB_makeSingleUser(ibuf);
    ibuf = preprocessed_ibuf;

    IMB_filtery(preprocessed_ibuf);
  }

  if (sequencer_use_crop(seq) || sequencer_use_transform(seq) || context->rectx != ibuf->x ||
      context->recty != ibuf->y) {
    const int x = context->rectx;
    const int y = context->recty;
    preprocessed_ibuf = IMB_allocImBuf(x, y, 32, ibuf->rect_float ? IB_rectfloat : IB_rect);

    sequencer_preprocess_transform_crop(ibuf, preprocessed_ibuf, context, seq, is_proxy_image);

    seq_imbuf_assign_spaces(scene, preprocessed_ibuf);
    IMB_metadata_copy(preprocessed_ibuf, ibuf);
    IMB_freeImBuf(ibuf);
  }

  /* Duplicate ibuf if we still have original. */
  if (preprocessed_ibuf == NULL) {
    preprocessed_ibuf = IMB_makeSingleUser(ibuf);
  }

  if (seq->flag & SEQ_FLIPX) {
    IMB_flipx(preprocessed_ibuf);
  }

  if (seq->flag & SEQ_FLIPY) {
    IMB_flipy(preprocessed_ibuf);
  }

  if (seq->sat != 1.0f) {
    IMB_saturation(preprocessed_ibuf, seq->sat);
  }

  if (seq->flag & SEQ_MAKE_FLOAT) {
    if (!preprocessed_ibuf->rect_float) {
      seq_imbuf_to_sequencer_space(scene, preprocessed_ibuf, true);
    }

    if (preprocessed_ibuf->rect) {
      imb_freerectImBuf(preprocessed_ibuf);
    }
  }

  float mul = seq->mul;
  if (seq->blend_mode == SEQ_BLEND_REPLACE) {
    mul *= seq->blend_opacity / 100.0f;
  }

  if (mul != 1.0f) {
    multibuf(preprocessed_ibuf, mul);
  }

  if (seq->modifiers.first) {
    ImBuf *ibuf_new = SEQ_modifier_apply_stack(context, seq, preprocessed_ibuf, timeline_frame);

    if (ibuf_new != preprocessed_ibuf) {
      IMB_metadata_copy(ibuf_new, preprocessed_ibuf);
      IMB_freeImBuf(preprocessed_ibuf);
      preprocessed_ibuf = ibuf_new;
    }
  }

  return preprocessed_ibuf;
}

static ImBuf *seq_render_preprocess_ibuf(const SeqRenderData *context,
                                         Sequence *seq,
                                         ImBuf *ibuf,
                                         float timeline_frame,
                                         bool use_preprocess,
                                         const bool is_proxy_image)
{
  if (context->is_proxy_render == false &&
      (ibuf->x != context->rectx || ibuf->y != context->recty)) {
    use_preprocess = true;
  }

  /* Proxies and effect strips are not stored in cache. */
  if (!is_proxy_image && (seq->type & SEQ_TYPE_EFFECT) == 0) {
    seq_cache_put(context, seq, timeline_frame, SEQ_CACHE_STORE_RAW, ibuf);
  }

  if (use_preprocess) {
    ibuf = input_preprocess(context, seq, timeline_frame, ibuf, is_proxy_image);
  }

  seq_cache_put(context, seq, timeline_frame, SEQ_CACHE_STORE_PREPROCESSED, ibuf);
  return ibuf;
}

typedef struct RenderEffectInitData {
  struct SeqEffectHandle *sh;
  const SeqRenderData *context;
  Sequence *seq;
  float timeline_frame, fac;
  ImBuf *ibuf1, *ibuf2, *ibuf3;

  ImBuf *out;
} RenderEffectInitData;

typedef struct RenderEffectThread {
  struct SeqEffectHandle *sh;
  const SeqRenderData *context;
  Sequence *seq;
  float timeline_frame, fac;
  ImBuf *ibuf1, *ibuf2, *ibuf3;

  ImBuf *out;
  int start_line, tot_line;
} RenderEffectThread;

static void render_effect_execute_init_handle(void *handle_v,
                                              int start_line,
                                              int tot_line,
                                              void *init_data_v)
{
  RenderEffectThread *handle = (RenderEffectThread *)handle_v;
  RenderEffectInitData *init_data = (RenderEffectInitData *)init_data_v;

  handle->sh = init_data->sh;
  handle->context = init_data->context;
  handle->seq = init_data->seq;
  handle->timeline_frame = init_data->timeline_frame;
  handle->fac = init_data->fac;
  handle->ibuf1 = init_data->ibuf1;
  handle->ibuf2 = init_data->ibuf2;
  handle->ibuf3 = init_data->ibuf3;
  handle->out = init_data->out;

  handle->start_line = start_line;
  handle->tot_line = tot_line;
}

static void *render_effect_execute_do_thread(void *thread_data_v)
{
  RenderEffectThread *thread_data = (RenderEffectThread *)thread_data_v;

  thread_data->sh->execute_slice(thread_data->context,
                                 thread_data->seq,
                                 thread_data->timeline_frame,
                                 thread_data->fac,
                                 thread_data->ibuf1,
                                 thread_data->ibuf2,
                                 thread_data->ibuf3,
                                 thread_data->start_line,
                                 thread_data->tot_line,
                                 thread_data->out);

  return NULL;
}

ImBuf *seq_render_effect_execute_threaded(struct SeqEffectHandle *sh,
                                          const SeqRenderData *context,
                                          Sequence *seq,
                                          float timeline_frame,
                                          float fac,
                                          ImBuf *ibuf1,
                                          ImBuf *ibuf2,
                                          ImBuf *ibuf3)
{
  RenderEffectInitData init_data;
  ImBuf *out = sh->init_execution(context, ibuf1, ibuf2, ibuf3);

  init_data.sh = sh;
  init_data.context = context;
  init_data.seq = seq;
  init_data.timeline_frame = timeline_frame;
  init_data.fac = fac;
  init_data.ibuf1 = ibuf1;
  init_data.ibuf2 = ibuf2;
  init_data.ibuf3 = ibuf3;
  init_data.out = out;

  IMB_processor_apply_threaded(out->y,
                               sizeof(RenderEffectThread),
                               &init_data,
                               render_effect_execute_init_handle,
                               render_effect_execute_do_thread);

  return out;
}

static ImBuf *seq_render_effect_strip_impl(const SeqRenderData *context,
                                           SeqRenderState *state,
                                           Sequence *seq,
                                           float timeline_frame)
{
  Scene *scene = context->scene;
  float fac;
  int early_out;
  int i;
  struct SeqEffectHandle sh = SEQ_effect_handle_get(seq);
  FCurve *fcu = NULL;
  ImBuf *ibuf[3];
  Sequence *input[3];
  ImBuf *out = NULL;

  ibuf[0] = ibuf[1] = ibuf[2] = NULL;

  input[0] = seq->seq1;
  input[1] = seq->seq2;
  input[2] = seq->seq3;

  if (!sh.execute && !(sh.execute_slice && sh.init_execution)) {
    /* effect not supported in this version... */
    out = IMB_allocImBuf(context->rectx, context->recty, 32, IB_rect);
    return out;
  }

  if (seq->flag & SEQ_USE_EFFECT_DEFAULT_FADE) {
    sh.get_default_fac(scene, seq, timeline_frame, &fac);
  }
  else {
    fcu = id_data_find_fcurve(&scene->id, seq, &RNA_Sequence, "effect_fader", 0, NULL);
    if (fcu) {
      fac = evaluate_fcurve(fcu, timeline_frame);
    }
    else {
      fac = seq->effect_fader;
    }
  }

  early_out = sh.early_out(seq, fac);

  switch (early_out) {
    case EARLY_NO_INPUT:
      out = sh.execute(context, seq, timeline_frame, fac, NULL, NULL, NULL);
      break;
    case EARLY_DO_EFFECT:
      for (i = 0; i < 3; i++) {
        /* Speed effect requires time remapping of `timeline_frame` for input(s). */
        if (input[0] && seq->type == SEQ_TYPE_SPEED) {
          float target_frame = seq_speed_effect_target_frame_get(scene, seq, timeline_frame, i);
          ibuf[i] = seq_render_strip(context, state, input[0], target_frame);
        }
        else { /* Other effects. */
          if (input[i]) {
            ibuf[i] = seq_render_strip(context, state, input[i], timeline_frame);
          }
        }
      }

      if (ibuf[0] && (ibuf[1] || SEQ_effect_get_num_inputs(seq->type) == 1)) {
        if (sh.multithreaded) {
          out = seq_render_effect_execute_threaded(
              &sh, context, seq, timeline_frame, fac, ibuf[0], ibuf[1], ibuf[2]);
        }
        else {
          out = sh.execute(context, seq, timeline_frame, fac, ibuf[0], ibuf[1], ibuf[2]);
        }
      }
      break;
    case EARLY_USE_INPUT_1:
      if (input[0]) {
        out = seq_render_strip(context, state, input[0], timeline_frame);
      }
      break;
    case EARLY_USE_INPUT_2:
      if (input[1]) {
        out = seq_render_strip(context, state, input[1], timeline_frame);
      }
      break;
  }

  for (i = 0; i < 3; i++) {
    IMB_freeImBuf(ibuf[i]);
  }

  if (out == NULL) {
    out = IMB_allocImBuf(context->rectx, context->recty, 32, IB_rect);
  }

  return out;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Individual strip rendering functions
 * \{ */

/**
 * Render individual view for multi-view or single (default view) for mono-view.
 */
static ImBuf *seq_render_image_strip_view(const SeqRenderData *context,
                                          Sequence *seq,
                                          char *name,
                                          char *prefix,
                                          const char *ext,
                                          int view_id)
{

  ImBuf *ibuf = NULL;

  int flag = IB_rect | IB_metadata;
  if (seq->alpha_mode == SEQ_ALPHA_PREMUL) {
    flag |= IB_alphamode_premul;
  }

  if (prefix[0] == '\0') {
    ibuf = IMB_loadiffname(name, flag, seq->strip->colorspace_settings.name);
  }
  else {
    char str[FILE_MAX];
    BKE_scene_multiview_view_prefix_get(context->scene, name, prefix, &ext);
    seq_multiview_name(context->scene, view_id, prefix, ext, str, FILE_MAX);
    ibuf = IMB_loadiffname(str, flag, seq->strip->colorspace_settings.name);
  }

  if (ibuf == NULL) {
    return NULL;
  }

  /* We don't need both (speed reasons)! */
  if (ibuf->rect_float != NULL && ibuf->rect != NULL) {
    imb_freerectImBuf(ibuf);
  }

  /* All sequencer color is done in SRGB space, linear gives odd cross-fades. */
  seq_imbuf_to_sequencer_space(context->scene, ibuf, false);

  return ibuf;
}

static bool seq_image_strip_is_multiview_render(
    Scene *scene, Sequence *seq, int totfiles, char *name, char *r_prefix, const char *r_ext)
{
  if (totfiles > 1) {
    BKE_scene_multiview_view_prefix_get(scene, name, r_prefix, &r_ext);
    if (r_prefix[0] == '\0') {
      return false;
    }
  }
  else {
    r_prefix[0] = '\0';
  }

  return (seq->flag & SEQ_USE_VIEWS) != 0 && (scene->r.scemode & R_MULTIVIEW) != 0;
}

static ImBuf *seq_render_image_strip(const SeqRenderData *context,
                                     Sequence *seq,
                                     float UNUSED(frame_index),
                                     float timeline_frame,
                                     bool *r_is_proxy_image)
{
  char name[FILE_MAX];
  const char *ext = NULL;
  char prefix[FILE_MAX];
  ImBuf *ibuf = NULL;

  StripElem *s_elem = SEQ_render_give_stripelem(context->scene, seq, timeline_frame);
  if (s_elem == NULL) {
    return NULL;
  }

  BLI_join_dirfile(name, sizeof(name), seq->strip->dir, s_elem->name);
  BLI_path_abs(name, BKE_main_blendfile_path_from_global());

  /* Try to get a proxy image. */
  ibuf = seq_proxy_fetch(context, seq, timeline_frame);
  if (ibuf != NULL) {
    *r_is_proxy_image = true;
    return ibuf;
  }

  /* Proxy not found, render original. */
  const int totfiles = seq_num_files(context->scene, seq->views_format, true);
  bool is_multiview_render = seq_image_strip_is_multiview_render(
      context->scene, seq, totfiles, name, prefix, ext);

  if (is_multiview_render) {
    int totviews = BKE_scene_multiview_num_views_get(&context->scene->r);
    ImBuf **ibufs_arr = MEM_callocN(sizeof(ImBuf *) * totviews, "Sequence Image Views Imbufs");

    for (int view_id = 0; view_id < totfiles; view_id++) {
      ibufs_arr[view_id] = seq_render_image_strip_view(context, seq, name, prefix, ext, view_id);
    }

    if (ibufs_arr[0] == NULL) {
      return NULL;
    }

    if (seq->views_format == R_IMF_VIEWS_STEREO_3D) {
      IMB_ImBufFromStereo3d(seq->stereo3d_format, ibufs_arr[0], &ibufs_arr[0], &ibufs_arr[1]);
    }

    for (int view_id = 0; view_id < totviews; view_id++) {
      SeqRenderData localcontext = *context;
      localcontext.view_id = view_id;

      if (view_id != context->view_id) {
        ibufs_arr[view_id] = seq_render_preprocess_ibuf(
            &localcontext, seq, ibufs_arr[view_id], timeline_frame, true, false);
      }
    }

    /* Return the original requested ImBuf. */
    ibuf = ibufs_arr[context->view_id];

    /* Remove the others (decrease their refcount). */
    for (int view_id = 0; view_id < totviews; view_id++) {
      if (ibufs_arr[view_id] != ibuf) {
        IMB_freeImBuf(ibufs_arr[view_id]);
      }
    }

    MEM_freeN(ibufs_arr);
  }
  else {
    ibuf = seq_render_image_strip_view(context, seq, name, prefix, ext, context->view_id);
  }

  if (ibuf == NULL) {
    return NULL;
  }

  s_elem->orig_width = ibuf->x;
  s_elem->orig_height = ibuf->y;

  return ibuf;
}

static ImBuf *seq_render_movie_strip_custom_file_proxy(const SeqRenderData *context,
                                                       Sequence *seq,
                                                       int timeline_frame)
{
  char name[PROXY_MAXFILE];
  StripProxy *proxy = seq->strip->proxy;

  if (proxy->anim == NULL) {
    if (seq_proxy_get_custom_file_fname(seq, name, context->view_id)) {
      proxy->anim = openanim(name, IB_rect, 0, seq->strip->colorspace_settings.name);
    }
    if (proxy->anim == NULL) {
      return NULL;
    }
  }

  int frameno = (int)seq_give_frame_index(context->scene, seq, timeline_frame) +
                seq->anim_startofs;
  return IMB_anim_absolute(proxy->anim, frameno, IMB_TC_NONE, IMB_PROXY_NONE);
}

/**
 * Render individual view for multi-view or single (default view) for mono-view.
 */
static ImBuf *seq_render_movie_strip_view(const SeqRenderData *context,
                                          Sequence *seq,
                                          float frame_index,
                                          float timeline_frame,
                                          StripAnim *sanim,
                                          bool *r_is_proxy_image)
{
  ImBuf *ibuf = NULL;
  IMB_Proxy_Size psize = SEQ_rendersize_to_proxysize(context->preview_render_size);

  if (SEQ_can_use_proxy(context, seq, psize)) {
    /* Try to get a proxy image.
     * Movie proxies are handled by ImBuf module with exception of `custom file` setting. */
    if (context->scene->ed->proxy_storage != SEQ_EDIT_PROXY_DIR_STORAGE &&
        seq->strip->proxy->storage & SEQ_STORAGE_PROXY_CUSTOM_FILE) {
      ibuf = seq_render_movie_strip_custom_file_proxy(context, seq, timeline_frame);
    }
    else {
      ibuf = IMB_anim_absolute(sanim->anim,
                               frame_index + seq->anim_startofs,
                               seq->strip->proxy ? seq->strip->proxy->tc : IMB_TC_RECORD_RUN,
                               psize);
    }

    if (ibuf != NULL) {
      *r_is_proxy_image = true;
    }
  }

  /* Fetching for requested proxy size failed, try fetching the original instead. */
  if (ibuf == NULL) {
    ibuf = IMB_anim_absolute(sanim->anim,
                             frame_index + seq->anim_startofs,
                             seq->strip->proxy ? seq->strip->proxy->tc : IMB_TC_RECORD_RUN,
                             IMB_PROXY_NONE);
  }
  if (ibuf == NULL) {
    return NULL;
  }

  seq_imbuf_to_sequencer_space(context->scene, ibuf, false);

  /* We don't need both (speed reasons)! */
  if (ibuf->rect_float != NULL && ibuf->rect != NULL) {
    imb_freerectImBuf(ibuf);
  }

  return ibuf;
}

static ImBuf *seq_render_movie_strip(const SeqRenderData *context,
                                     Sequence *seq,
                                     float frame_index,
                                     float timeline_frame,
                                     bool *r_is_proxy_image)
{
  /* Load all the videos. */
  seq_open_anim_file(context->scene, seq, false);

  ImBuf *ibuf = NULL;
  StripAnim *sanim = seq->anims.first;
  const int totfiles = seq_num_files(context->scene, seq->views_format, true);
  bool is_multiview_render = (seq->flag & SEQ_USE_VIEWS) != 0 &&
                             (context->scene->r.scemode & R_MULTIVIEW) != 0 &&
                             BLI_listbase_count_at_most(&seq->anims, totfiles + 1) == totfiles;

  if (is_multiview_render) {
    ImBuf **ibuf_arr;
    int totviews = BKE_scene_multiview_num_views_get(&context->scene->r);
    ibuf_arr = MEM_callocN(sizeof(ImBuf *) * totviews, "Sequence Image Views Imbufs");
    int ibuf_view_id;

    for (ibuf_view_id = 0, sanim = seq->anims.first; sanim; sanim = sanim->next, ibuf_view_id++) {
      if (sanim->anim) {
        ibuf_arr[ibuf_view_id] = seq_render_movie_strip_view(
            context, seq, frame_index, timeline_frame, sanim, r_is_proxy_image);
      }
    }

    if (seq->views_format == R_IMF_VIEWS_STEREO_3D) {
      if (ibuf_arr[0] == NULL) {
        /* Probably proxy hasn't been created yet. */
        MEM_freeN(ibuf_arr);
        return NULL;
      }

      IMB_ImBufFromStereo3d(seq->stereo3d_format, ibuf_arr[0], &ibuf_arr[0], &ibuf_arr[1]);
    }

    for (int view_id = 0; view_id < totviews; view_id++) {
      SeqRenderData localcontext = *context;
      localcontext.view_id = view_id;

      if (view_id != context->view_id) {
        ibuf_arr[view_id] = seq_render_preprocess_ibuf(
            &localcontext, seq, ibuf_arr[view_id], timeline_frame, true, false);
      }
    }

    /* Return the original requested ImBuf. */
    ibuf = ibuf_arr[context->view_id];

    /* Remove the others (decrease their refcount). */
    for (int view_id = 0; view_id < totviews; view_id++) {
      if (ibuf_arr[view_id] != ibuf) {
        IMB_freeImBuf(ibuf_arr[view_id]);
      }
    }

    MEM_freeN(ibuf_arr);
  }
  else {
    ibuf = seq_render_movie_strip_view(
        context, seq, frame_index, timeline_frame, sanim, r_is_proxy_image);
  }

  if (ibuf == NULL) {
    return NULL;
  }

  if (*r_is_proxy_image == false) {
    if (sanim && sanim->anim) {
      short fps_denom;
      float fps_num;
      IMB_anim_get_fps(sanim->anim, &fps_denom, &fps_num, true);
      seq->strip->stripdata->orig_fps = fps_denom / fps_num;
    }
    seq->strip->stripdata->orig_width = ibuf->x;
    seq->strip->stripdata->orig_height = ibuf->y;
  }

  return ibuf;
}

static ImBuf *seq_get_movieclip_ibuf(Sequence *seq, MovieClipUser user)
{
  ImBuf *ibuf = NULL;
  float tloc[2], tscale, tangle;
  if (seq->clip_flag & SEQ_MOVIECLIP_RENDER_STABILIZED) {
    ibuf = BKE_movieclip_get_stable_ibuf(seq->clip, &user, tloc, &tscale, &tangle, 0);
  }
  else {
    ibuf = BKE_movieclip_get_ibuf_flag(seq->clip, &user, seq->clip->flag, MOVIECLIP_CACHE_SKIP);
  }
  return ibuf;
}

static ImBuf *seq_render_movieclip_strip(const SeqRenderData *context,
                                         Sequence *seq,
                                         float frame_index,
                                         bool *r_is_proxy_image)
{
  ImBuf *ibuf = NULL;
  MovieClipUser user = *DNA_struct_default_get(MovieClipUser);
  IMB_Proxy_Size psize = SEQ_rendersize_to_proxysize(context->preview_render_size);

  if (!seq->clip) {
    return NULL;
  }

  BKE_movieclip_user_set_frame(&user, frame_index + seq->anim_startofs + seq->clip->start_frame);

  user.render_size = MCLIP_PROXY_RENDER_SIZE_FULL;
  switch (psize) {
    case IMB_PROXY_NONE:
      user.render_size = MCLIP_PROXY_RENDER_SIZE_FULL;
      break;
    case IMB_PROXY_100:
      user.render_size = MCLIP_PROXY_RENDER_SIZE_100;
      break;
    case IMB_PROXY_75:
      user.render_size = MCLIP_PROXY_RENDER_SIZE_75;
      break;
    case IMB_PROXY_50:
      user.render_size = MCLIP_PROXY_RENDER_SIZE_50;
      break;
    case IMB_PROXY_25:
      user.render_size = MCLIP_PROXY_RENDER_SIZE_25;
      break;
  }

  if (seq->clip_flag & SEQ_MOVIECLIP_RENDER_UNDISTORTED) {
    user.render_flag |= MCLIP_PROXY_RENDER_UNDISTORT;
  }

  /* Try to get a proxy image. */
  ibuf = seq_get_movieclip_ibuf(seq, user);

  /* If clip doesn't use proxies, it will fallback to full size render of original file. */
  if (ibuf != NULL && psize != IMB_PROXY_NONE && BKE_movieclip_proxy_enabled(seq->clip)) {
    *r_is_proxy_image = true;
  }

  /* If proxy is not found, grab full-size frame. */
  if (ibuf == NULL) {
    user.render_flag |= MCLIP_PROXY_RENDER_USE_FALLBACK_RENDER;
    ibuf = seq_get_movieclip_ibuf(seq, user);
  }

  return ibuf;
}

ImBuf *seq_render_mask(const SeqRenderData *context,
                       Mask *mask,
                       float frame_index,
                       bool make_float)
{
  /* TODO: add option to rasterize to alpha imbuf? */
  ImBuf *ibuf = NULL;
  float *maskbuf;
  int i;

  if (!mask) {
    return NULL;
  }

  AnimData *adt;
  Mask *mask_temp;
  MaskRasterHandle *mr_handle;

  mask_temp = (Mask *)BKE_id_copy_ex(
      NULL, &mask->id, NULL, LIB_ID_COPY_LOCALIZE | LIB_ID_COPY_NO_ANIMDATA);

  BKE_mask_evaluate(mask_temp, mask->sfra + frame_index, true);

  /* anim-data */
  adt = BKE_animdata_from_id(&mask->id);
  const AnimationEvalContext anim_eval_context = BKE_animsys_eval_context_construct(
      context->depsgraph, mask->sfra + frame_index);
  BKE_animsys_evaluate_animdata(&mask_temp->id, adt, &anim_eval_context, ADT_RECALC_ANIM, false);

  maskbuf = MEM_mallocN(sizeof(float) * context->rectx * context->recty, __func__);

  mr_handle = BKE_maskrasterize_handle_new();

  BKE_maskrasterize_handle_init(
      mr_handle, mask_temp, context->rectx, context->recty, true, true, true);

  BKE_id_free(NULL, &mask_temp->id);

  BKE_maskrasterize_buffer(mr_handle, context->rectx, context->recty, maskbuf);

  BKE_maskrasterize_handle_free(mr_handle);

  if (make_float) {
    /* pixels */
    const float *fp_src;
    float *fp_dst;

    ibuf = IMB_allocImBuf(context->rectx, context->recty, 32, IB_rectfloat);

    fp_src = maskbuf;
    fp_dst = ibuf->rect_float;
    i = context->rectx * context->recty;
    while (--i) {
      fp_dst[0] = fp_dst[1] = fp_dst[2] = *fp_src;
      fp_dst[3] = 1.0f;

      fp_src += 1;
      fp_dst += 4;
    }
  }
  else {
    /* pixels */
    const float *fp_src;
    unsigned char *ub_dst;

    ibuf = IMB_allocImBuf(context->rectx, context->recty, 32, IB_rect);

    fp_src = maskbuf;
    ub_dst = (unsigned char *)ibuf->rect;
    i = context->rectx * context->recty;
    while (--i) {
      ub_dst[0] = ub_dst[1] = ub_dst[2] = (unsigned char)(*fp_src * 255.0f); /* already clamped */
      ub_dst[3] = 255;

      fp_src += 1;
      ub_dst += 4;
    }
  }

  MEM_freeN(maskbuf);

  return ibuf;
}

static ImBuf *seq_render_mask_strip(const SeqRenderData *context, Sequence *seq, float frame_index)
{
  bool make_float = (seq->flag & SEQ_MAKE_FLOAT) != 0;

  return seq_render_mask(context, seq->mask, frame_index, make_float);
}

static ImBuf *seq_render_scene_strip(const SeqRenderData *context,
                                     Sequence *seq,
                                     float frame_index,
                                     float timeline_frame)
{
  ImBuf *ibuf = NULL;
  double frame;
  Object *camera;

  struct {
    int scemode;
    int timeline_frame;
    float subframe;

#ifdef DURIAN_CAMERA_SWITCH
    int mode;
#endif
  } orig_data;

  /* Old info:
   * Hack! This function can be called from do_render_seq(), in that case
   * the seq->scene can already have a Render initialized with same name,
   * so we have to use a default name. (compositor uses scene name to
   * find render).
   * However, when called from within the UI (image preview in sequencer)
   * we do want to use scene Render, that way the render result is defined
   * for display in render/imagewindow
   *
   * Hmm, don't see, why we can't do that all the time,
   * and since G.is_rendering is uhm, gone... (Peter)
   */

  /* New info:
   * Using the same name for the renders works just fine as the do_render_seq()
   * render is not used while the scene strips are rendered.
   *
   * However rendering from UI (through sequencer_preview_area_draw) can crash in
   * very many cases since other renders (material preview, an actual render etc.)
   * can be started while this sequence preview render is running. The only proper
   * solution is to make the sequencer preview render a proper job, which can be
   * stopped when needed. This would also give a nice progress bar for the preview
   * space so that users know there's something happening.
   *
   * As a result the active scene now only uses OpenGL rendering for the sequencer
   * preview. This is far from nice, but is the only way to prevent crashes at this
   * time.
   *
   * -jahka
   */

  const bool is_rendering = G.is_rendering;
  const bool is_background = G.background;
  const bool do_seq_gl = is_rendering ? 0 : (context->scene->r.seq_prev_type) != OB_RENDER;
  bool have_comp = false;
  bool use_gpencil = true;
  /* do we need to re-evaluate the frame after rendering? */
  bool is_frame_update = false;
  Scene *scene;
  int is_thread_main = BLI_thread_is_main();

  /* don't refer to seq->scene above this point!, it can be NULL */
  if (seq->scene == NULL) {
    return NULL;
  }

  /* Prevent rendering scene recursively. */
  if (seq->scene == context->scene) {
    return NULL;
  }

  scene = seq->scene;
  frame = (double)scene->r.sfra + (double)frame_index + (double)seq->anim_startofs;

#if 0 /* UNUSED */
  have_seq = (scene->r.scemode & R_DOSEQ) && scene->ed && scene->ed->seqbase.first);
#endif
  have_comp = (scene->r.scemode & R_DOCOMP) && scene->use_nodes && scene->nodetree;

  /* Get view layer for the strip. */
  ViewLayer *view_layer = BKE_view_layer_default_render(scene);
  /* Depsgraph will be NULL when doing rendering. */
  Depsgraph *depsgraph = NULL;

  orig_data.scemode = scene->r.scemode;
  orig_data.timeline_frame = scene->r.cfra;
  orig_data.subframe = scene->r.subframe;
#ifdef DURIAN_CAMERA_SWITCH
  orig_data.mode = scene->r.mode;
#endif

  BKE_scene_frame_set(scene, frame);

  if (seq->scene_camera) {
    camera = seq->scene_camera;
  }
  else {
    BKE_scene_camera_switch_update(scene);
    camera = scene->camera;
  }

  if (have_comp == false && camera == NULL) {
    goto finally;
  }

  if (seq->flag & SEQ_SCENE_NO_ANNOTATION) {
    use_gpencil = false;
  }

  /* prevent eternal loop */
  scene->r.scemode &= ~R_DOSEQ;

#ifdef DURIAN_CAMERA_SWITCH
  /* stooping to new low's in hackyness :( */
  scene->r.mode |= R_NO_CAMERA_SWITCH;
#endif

  is_frame_update = (orig_data.timeline_frame != scene->r.cfra) ||
                    (orig_data.subframe != scene->r.subframe);

  if ((sequencer_view3d_fn && do_seq_gl && camera) && is_thread_main) {
    char err_out[256] = "unknown";
    int width, height;
    BKE_render_resolution(&scene->r, false, &width, &height);
    const char *viewname = BKE_scene_multiview_render_view_name_get(&scene->r, context->view_id);

    unsigned int draw_flags = V3D_OFSDRAW_NONE;
    draw_flags |= (use_gpencil) ? V3D_OFSDRAW_SHOW_ANNOTATION : 0;
    draw_flags |= (context->scene->r.seq_flag & R_SEQ_OVERRIDE_SCENE_SETTINGS) ?
                      V3D_OFSDRAW_OVERRIDE_SCENE_SETTINGS :
                      0;

    /* for old scene this can be uninitialized,
     * should probably be added to do_versions at some point if the functionality stays */
    if (context->scene->r.seq_prev_type == 0) {
      context->scene->r.seq_prev_type = 3 /* == OB_SOLID */;
    }

    /* opengl offscreen render */
    depsgraph = BKE_scene_ensure_depsgraph(context->bmain, scene, view_layer);
    BKE_scene_graph_update_for_newframe(depsgraph);
    Object *camera_eval = DEG_get_evaluated_object(depsgraph, camera);
    ibuf = sequencer_view3d_fn(
        /* set for OpenGL render (NULL when scrubbing) */
        depsgraph,
        scene,
        &context->scene->display.shading,
        context->scene->r.seq_prev_type,
        camera_eval,
        width,
        height,
        IB_rect,
        draw_flags,
        scene->r.alphamode,
        viewname,
        context->gpu_offscreen,
        err_out);
    if (ibuf == NULL) {
      fprintf(stderr, "seq_render_scene_strip failed to get opengl buffer: %s\n", err_out);
    }
  }
  else {
    Render *re = RE_GetSceneRender(scene);
    const int totviews = BKE_scene_multiview_num_views_get(&scene->r);
    ImBuf **ibufs_arr;

    ibufs_arr = MEM_callocN(sizeof(ImBuf *) * totviews, "Sequence Image Views Imbufs");

    /* XXX: this if can be removed when sequence preview rendering uses the job system
     *
     * disable rendered preview for sequencer while rendering -- it's very much possible
     * that preview render will went into conflict with final render
     *
     * When rendering from command line renderer is called from main thread, in this
     * case it's always safe to render scene here
     */
    if (!is_thread_main || is_rendering == false || is_background || context->for_render) {
      if (re == NULL) {
        re = RE_NewSceneRender(scene);
      }

      const float subframe = frame - floorf(frame);

      RE_RenderFrame(re,
                     context->bmain,
                     scene,
                     have_comp ? NULL : view_layer,
                     camera,
                     floorf(frame),
                     subframe,
                     false);

      /* restore previous state after it was toggled on & off by RE_RenderFrame */
      G.is_rendering = is_rendering;
    }

    for (int view_id = 0; view_id < totviews; view_id++) {
      SeqRenderData localcontext = *context;
      RenderResult rres;

      localcontext.view_id = view_id;

      RE_AcquireResultImage(re, &rres, view_id);

      if (rres.rectf) {
        ibufs_arr[view_id] = IMB_allocImBuf(rres.rectx, rres.recty, 32, IB_rectfloat);
        memcpy(ibufs_arr[view_id]->rect_float,
               rres.rectf,
               sizeof(float[4]) * rres.rectx * rres.recty);

        if (rres.rectz) {
          addzbuffloatImBuf(ibufs_arr[view_id]);
          memcpy(
              ibufs_arr[view_id]->zbuf_float, rres.rectz, sizeof(float) * rres.rectx * rres.recty);
        }

        /* float buffers in the sequencer are not linear */
        seq_imbuf_to_sequencer_space(context->scene, ibufs_arr[view_id], false);
      }
      else if (rres.rect32) {
        ibufs_arr[view_id] = IMB_allocImBuf(rres.rectx, rres.recty, 32, IB_rect);
        memcpy(ibufs_arr[view_id]->rect, rres.rect32, 4 * rres.rectx * rres.recty);
      }

      if (view_id != context->view_id) {
        seq_cache_put(&localcontext, seq, timeline_frame, SEQ_CACHE_STORE_RAW, ibufs_arr[view_id]);
      }

      RE_ReleaseResultImage(re);
    }

    /* return the original requested ImBuf */
    ibuf = ibufs_arr[context->view_id];

    /* "remove" the others (decrease their refcount) */
    for (int view_id = 0; view_id < totviews; view_id++) {
      if (ibufs_arr[view_id] != ibuf) {
        IMB_freeImBuf(ibufs_arr[view_id]);
      }
    }
    MEM_freeN(ibufs_arr);
  }

finally:
  /* restore */
  scene->r.scemode = orig_data.scemode;
  scene->r.cfra = orig_data.timeline_frame;
  scene->r.subframe = orig_data.subframe;

  if (is_frame_update && (depsgraph != NULL)) {
    BKE_scene_graph_update_for_newframe(depsgraph);
  }

#ifdef DURIAN_CAMERA_SWITCH
  /* stooping to new low's in hackyness :( */
  scene->r.mode &= orig_data.mode | ~R_NO_CAMERA_SWITCH;
#endif

  return ibuf;
}

/**
 * Used for meta-strips & scenes with #SEQ_SCENE_STRIPS flag set.
 */
static ImBuf *do_render_strip_seqbase(const SeqRenderData *context,
                                      SeqRenderState *state,
                                      Sequence *seq,
                                      float frame_index)
{
  ImBuf *ibuf = NULL;
  ListBase *seqbase = NULL;
  ListBase *channels = NULL;
  int offset;

  seqbase = SEQ_get_seqbase_from_sequence(seq, &channels, &offset);

  if (seqbase && !BLI_listbase_is_empty(seqbase)) {

    if (seq->flag & SEQ_SCENE_STRIPS && seq->scene) {
      BKE_animsys_evaluate_all_animation(context->bmain, context->depsgraph, frame_index + offset);
    }

    ibuf = seq_render_strip_stack(context,
                                  state,
                                  channels,
                                  seqbase,
                                  /* scene strips don't have their start taken into account */
                                  frame_index + offset,
                                  0);
  }

  return ibuf;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Strip Stack Rendering Functions
 * \{ */

static ImBuf *do_render_strip_uncached(const SeqRenderData *context,
                                       SeqRenderState *state,
                                       Sequence *seq,
                                       float timeline_frame,
                                       bool *r_is_proxy_image)
{
  ImBuf *ibuf = NULL;
  float frame_index = seq_give_frame_index(context->scene, seq, timeline_frame);
  int type = (seq->type & SEQ_TYPE_EFFECT) ? SEQ_TYPE_EFFECT : seq->type;
  switch (type) {
    case SEQ_TYPE_META: {
      ibuf = do_render_strip_seqbase(context, state, seq, frame_index);
      break;
    }

    case SEQ_TYPE_SCENE: {
      if (seq->flag & SEQ_SCENE_STRIPS) {
        if (seq->scene && (context->scene != seq->scene)) {
          /* recursive check */
          if (BLI_linklist_index(state->scene_parents, seq->scene) != -1) {
            break;
          }
          LinkNode scene_parent = {
              .next = state->scene_parents,
              .link = seq->scene,
          };
          state->scene_parents = &scene_parent;
          /* end check */

          /* Use the Scene sequence-strip's scene for the context when rendering the
           * scene's sequences (necessary for multi-cam selector among others). */
          SeqRenderData local_context = *context;
          local_context.scene = seq->scene;
          local_context.skip_cache = true;

          ibuf = do_render_strip_seqbase(&local_context, state, seq, frame_index);

          /* step back in the list */
          state->scene_parents = state->scene_parents->next;
        }
      }
      else {
        /* scene can be NULL after deletions */
        ibuf = seq_render_scene_strip(context, seq, frame_index, timeline_frame);
      }

      break;
    }

    case SEQ_TYPE_EFFECT: {
      ibuf = seq_render_effect_strip_impl(context, state, seq, timeline_frame);
      break;
    }

    case SEQ_TYPE_IMAGE: {
      ibuf = seq_render_image_strip(context, seq, frame_index, timeline_frame, r_is_proxy_image);
      break;
    }

    case SEQ_TYPE_MOVIE: {
      ibuf = seq_render_movie_strip(context, seq, frame_index, timeline_frame, r_is_proxy_image);
      break;
    }

    case SEQ_TYPE_MOVIECLIP: {
      ibuf = seq_render_movieclip_strip(context, seq, frame_index, r_is_proxy_image);

      if (ibuf) {
        /* duplicate frame so movie cache wouldn't be confused by sequencer's stuff */
        ImBuf *i = IMB_dupImBuf(ibuf);
        IMB_freeImBuf(ibuf);
        ibuf = i;

        if (ibuf->rect_float) {
          seq_imbuf_to_sequencer_space(context->scene, ibuf, false);
        }
      }

      break;
    }

    case SEQ_TYPE_MASK: {
      /* ibuf is always new */
      ibuf = seq_render_mask_strip(context, seq, frame_index);
      break;
    }
  }

  if (ibuf) {
    seq_imbuf_assign_spaces(context->scene, ibuf);
  }

  return ibuf;
}

ImBuf *seq_render_strip(const SeqRenderData *context,
                        SeqRenderState *state,
                        Sequence *seq,
                        float timeline_frame)
{
  ImBuf *ibuf = NULL;
  bool use_preprocess = false;
  bool is_proxy_image = false;

  ibuf = seq_cache_get(context, seq, timeline_frame, SEQ_CACHE_STORE_PREPROCESSED);
  if (ibuf != NULL) {
    return ibuf;
  }

  /* Proxies are not stored in cache. */
  if (!SEQ_can_use_proxy(
          context, seq, SEQ_rendersize_to_proxysize(context->preview_render_size))) {
    ibuf = seq_cache_get(context, seq, timeline_frame, SEQ_CACHE_STORE_RAW);
  }

  if (ibuf == NULL) {
    ibuf = do_render_strip_uncached(context, state, seq, timeline_frame, &is_proxy_image);
  }

  if (ibuf) {
    use_preprocess = seq_input_have_to_preprocess(context, seq, timeline_frame);
    ibuf = seq_render_preprocess_ibuf(
        context, seq, ibuf, timeline_frame, use_preprocess, is_proxy_image);
  }

  if (ibuf == NULL) {
    ibuf = IMB_allocImBuf(context->rectx, context->recty, 32, IB_rect);
    seq_imbuf_assign_spaces(context->scene, ibuf);
  }

  return ibuf;
}

static bool seq_must_swap_input_in_blend_mode(Sequence *seq)
{
  bool swap_input = false;

  /* bad hack, to fix crazy input ordering of
   * those two effects */

  if (ELEM(seq->blend_mode, SEQ_TYPE_ALPHAOVER, SEQ_TYPE_ALPHAUNDER, SEQ_TYPE_OVERDROP)) {
    swap_input = true;
  }

  return swap_input;
}

static int seq_get_early_out_for_blend_mode(Sequence *seq)
{
  struct SeqEffectHandle sh = seq_effect_get_sequence_blend(seq);
  float fac = seq->blend_opacity / 100.0f;
  int early_out = sh.early_out(seq, fac);

  if (ELEM(early_out, EARLY_DO_EFFECT, EARLY_NO_INPUT)) {
    return early_out;
  }

  if (seq_must_swap_input_in_blend_mode(seq)) {
    if (early_out == EARLY_USE_INPUT_2) {
      return EARLY_USE_INPUT_1;
    }
    if (early_out == EARLY_USE_INPUT_1) {
      return EARLY_USE_INPUT_2;
    }
  }
  return early_out;
}

static ImBuf *seq_render_strip_stack_apply_effect(
    const SeqRenderData *context, Sequence *seq, float timeline_frame, ImBuf *ibuf1, ImBuf *ibuf2)
{
  ImBuf *out;
  struct SeqEffectHandle sh = seq_effect_get_sequence_blend(seq);
  float fac = seq->blend_opacity / 100.0f;
  int swap_input = seq_must_swap_input_in_blend_mode(seq);

  if (swap_input) {
    if (sh.multithreaded) {
      out = seq_render_effect_execute_threaded(
          &sh, context, seq, timeline_frame, fac, ibuf2, ibuf1, NULL);
    }
    else {
      out = sh.execute(context, seq, timeline_frame, fac, ibuf2, ibuf1, NULL);
    }
  }
  else {
    if (sh.multithreaded) {
      out = seq_render_effect_execute_threaded(
          &sh, context, seq, timeline_frame, fac, ibuf1, ibuf2, NULL);
    }
    else {
      out = sh.execute(context, seq, timeline_frame, fac, ibuf1, ibuf2, NULL);
    }
  }

  return out;
}

static ImBuf *seq_render_strip_stack(const SeqRenderData *context,
                                     SeqRenderState *state,
                                     ListBase *channels,
                                     ListBase *seqbasep,
                                     float timeline_frame,
                                     int chanshown)
{
  Sequence *seq_arr[MAXSEQ + 1];
  int count;
  int i;
  ImBuf *out = NULL;

  count = seq_get_shown_sequences(
      context->scene, channels, seqbasep, timeline_frame, chanshown, (Sequence **)&seq_arr);

  if (count == 0) {
    return NULL;
  }

  for (i = count - 1; i >= 0; i--) {
    int early_out;
    Sequence *seq = seq_arr[i];

    out = seq_cache_get(context, seq, timeline_frame, SEQ_CACHE_STORE_COMPOSITE);

    if (out) {
      break;
    }
    if (seq->blend_mode == SEQ_BLEND_REPLACE) {
      out = seq_render_strip(context, state, seq, timeline_frame);
      break;
    }

    early_out = seq_get_early_out_for_blend_mode(seq);

    /* Early out for alpha over. It requires image to be rendered, so it can't use
     * `seq_get_early_out_for_blend_mode`. */
    if (out == NULL && seq->blend_mode == SEQ_TYPE_ALPHAOVER && seq->blend_opacity == 100.0f) {
      ImBuf *test = seq_render_strip(context, state, seq, timeline_frame);
      if (ELEM(test->planes, R_IMF_PLANES_BW, R_IMF_PLANES_RGB)) {
        early_out = EARLY_USE_INPUT_2;
      }
      else {
        early_out = EARLY_DO_EFFECT;
      }
      /* Free the image. It is stored in cache, so this doesn't affect performance. */
      IMB_freeImBuf(test);
    }

    switch (early_out) {
      case EARLY_NO_INPUT:
      case EARLY_USE_INPUT_2:
        out = seq_render_strip(context, state, seq, timeline_frame);
        break;
      case EARLY_USE_INPUT_1:
        if (i == 0) {
          out = IMB_allocImBuf(context->rectx, context->recty, 32, IB_rect);
        }
        break;
      case EARLY_DO_EFFECT:
        if (i == 0) {
          ImBuf *ibuf1 = IMB_allocImBuf(context->rectx, context->recty, 32, IB_rect);
          ImBuf *ibuf2 = seq_render_strip(context, state, seq, timeline_frame);

          out = seq_render_strip_stack_apply_effect(context, seq, timeline_frame, ibuf1, ibuf2);

          seq_cache_put(context, seq_arr[i], timeline_frame, SEQ_CACHE_STORE_COMPOSITE, out);

          IMB_freeImBuf(ibuf1);
          IMB_freeImBuf(ibuf2);
        }
        break;
    }

    if (out) {
      break;
    }
  }

  i++;
  for (; i < count; i++) {
    Sequence *seq = seq_arr[i];

    if (seq_get_early_out_for_blend_mode(seq) == EARLY_DO_EFFECT) {
      ImBuf *ibuf1 = out;
      ImBuf *ibuf2 = seq_render_strip(context, state, seq, timeline_frame);

      out = seq_render_strip_stack_apply_effect(context, seq, timeline_frame, ibuf1, ibuf2);

      IMB_freeImBuf(ibuf1);
      IMB_freeImBuf(ibuf2);
    }

    seq_cache_put(context, seq_arr[i], timeline_frame, SEQ_CACHE_STORE_COMPOSITE, out);
  }

  return out;
}

ImBuf *SEQ_render_give_ibuf(const SeqRenderData *context, float timeline_frame, int chanshown)
{
  Scene *scene = context->scene;
  Editing *ed = SEQ_editing_get(scene);
  ListBase *seqbasep;
  ListBase *channels;

  if (ed == NULL) {
    return NULL;
  }

  if ((chanshown < 0) && !BLI_listbase_is_empty(&ed->metastack)) {
    int count = BLI_listbase_count(&ed->metastack);
    count = max_ii(count + chanshown, 0);
    seqbasep = ((MetaStack *)BLI_findlink(&ed->metastack, count))->oldbasep;
    channels = ((MetaStack *)BLI_findlink(&ed->metastack, count))->old_channels;
  }
  else {
    seqbasep = ed->seqbasep;
    channels = ed->displayed_channels;
  }

  SeqRenderState state;
  seq_render_state_init(&state);
  ImBuf *out = NULL;
  Sequence *seq_arr[MAXSEQ + 1];
  int count;

  count = seq_get_shown_sequences(scene, channels, seqbasep, timeline_frame, chanshown, seq_arr);

  if (count) {
    out = seq_cache_get(context, seq_arr[count - 1], timeline_frame, SEQ_CACHE_STORE_FINAL_OUT);
  }

  seq_cache_free_temp_cache(context->scene, context->task_id, timeline_frame);
  /* Make sure we only keep the `anim` data for strips that are in view. */
  SEQ_relations_free_all_anim_ibufs(context->scene, timeline_frame);

  if (count && !out) {
    BLI_mutex_lock(&seq_render_mutex);
    out = seq_render_strip_stack(context, &state, channels, seqbasep, timeline_frame, chanshown);

    if (context->is_prefetch_render) {
      seq_cache_put(context, seq_arr[count - 1], timeline_frame, SEQ_CACHE_STORE_FINAL_OUT, out);
    }
    else {
      seq_cache_put_if_possible(
          context, seq_arr[count - 1], timeline_frame, SEQ_CACHE_STORE_FINAL_OUT, out);
    }
    BLI_mutex_unlock(&seq_render_mutex);
  }

  seq_prefetch_start(context, timeline_frame);

  return out;
}

ImBuf *seq_render_give_ibuf_seqbase(const SeqRenderData *context,
                                    float timeline_frame,
                                    int chan_shown,
                                    ListBase *channels,
                                    ListBase *seqbasep)
{
  SeqRenderState state;
  seq_render_state_init(&state);

  return seq_render_strip_stack(context, &state, channels, seqbasep, timeline_frame, chan_shown);
}

ImBuf *SEQ_render_give_ibuf_direct(const SeqRenderData *context,
                                   float timeline_frame,
                                   Sequence *seq)
{
  SeqRenderState state;
  seq_render_state_init(&state);

  ImBuf *ibuf = seq_render_strip(context, &state, seq, timeline_frame);
  return ibuf;
}

float SEQ_render_thumbnail_first_frame_get(const Scene *scene,
                                           Sequence *seq,
                                           float frame_step,
                                           rctf *view_area)
{
  int first_drawable_frame = max_iii(
      SEQ_time_left_handle_frame_get(scene, seq), seq->start, view_area->xmin);

  /* First frame should correspond to handle position. */
  if (first_drawable_frame == SEQ_time_left_handle_frame_get(scene, seq)) {
    return SEQ_time_left_handle_frame_get(scene, seq);
  }

  float aligned_frame_offset = (int)((first_drawable_frame - seq->start) / frame_step) *
                               frame_step;
  return seq->start + aligned_frame_offset;
}

float SEQ_render_thumbnail_next_frame_get(const Scene *scene,
                                          Sequence *seq,
                                          float last_frame,
                                          float frame_step)
{
  float next_frame = last_frame + frame_step;

  /* If handle position was displayed, align next frame with `seq->start`. */
  if (last_frame == SEQ_time_left_handle_frame_get(scene, seq)) {
    next_frame = seq->start + ((int)((last_frame - seq->start) / frame_step) + 1) * frame_step;
  }

  return next_frame;
}

/* Gets the direct image from source and scales to thumbnail size. */
static ImBuf *seq_get_uncached_thumbnail(const SeqRenderData *context,
                                         SeqRenderState *state,
                                         Sequence *seq,
                                         float timeline_frame)
{
  bool is_proxy_image = false;
  ImBuf *ibuf = do_render_strip_uncached(context, state, seq, timeline_frame, &is_proxy_image);

  if (ibuf == NULL) {
    return NULL;
  }

  float aspect_ratio = (float)ibuf->x / ibuf->y;
  int rectx, recty;
  /* Calculate new dimensions - THUMB_SIZE (256) for x or y. */
  if (ibuf->x > ibuf->y) {
    rectx = SEQ_RENDER_THUMB_SIZE;
    recty = round_fl_to_int(rectx / aspect_ratio);
  }
  else {
    recty = SEQ_RENDER_THUMB_SIZE;
    rectx = round_fl_to_int(recty * aspect_ratio);
  }

  /* Scale ibuf to thumbnail size. */
  ImBuf *scaled_ibuf = IMB_allocImBuf(rectx, recty, 32, ibuf->rect_float ? IB_rectfloat : IB_rect);
  sequencer_thumbnail_transform(ibuf, scaled_ibuf);
  seq_imbuf_assign_spaces(context->scene, scaled_ibuf);
  IMB_freeImBuf(ibuf);

  return scaled_ibuf;
}

ImBuf *SEQ_get_thumbnail(
    const SeqRenderData *context, Sequence *seq, float timeline_frame, rcti *crop, bool clipped)
{
  ImBuf *ibuf = seq_cache_get(context, seq, roundf(timeline_frame), SEQ_CACHE_STORE_THUMBNAIL);

  if (!clipped || ibuf == NULL) {
    return ibuf;
  }

  /* Do clipping. */
  ImBuf *ibuf_cropped = IMB_dupImBuf(ibuf);
  if (crop->xmin < 0 || crop->ymin < 0) {
    crop->xmin = 0;
    crop->ymin = 0;
  }
  if (crop->xmax >= ibuf->x || crop->ymax >= ibuf->y) {
    crop->xmax = ibuf->x - 1;
    crop->ymax = ibuf->y - 1;
  }
  IMB_rect_crop(ibuf_cropped, crop);
  IMB_freeImBuf(ibuf);
  return ibuf_cropped;
}

void SEQ_render_thumbnails(const SeqRenderData *context,
                           Sequence *seq,
                           Sequence *seq_orig,
                           float frame_step,
                           rctf *view_area,
                           const short *stop)
{
  SeqRenderState state;
  seq_render_state_init(&state);
  const Scene *scene = context->scene;

  /* Adding the hold offset value (seq->anim_startofs) to the start frame. Position of image not
   * affected, but frame loaded affected. */
  float upper_thumb_bound = SEQ_time_has_right_still_frames(scene, seq) ?
                                (seq->start + seq->len) :
                                SEQ_time_right_handle_frame_get(scene, seq);
  upper_thumb_bound = (upper_thumb_bound > view_area->xmax) ? view_area->xmax + frame_step :
                                                              upper_thumb_bound;

  float timeline_frame = SEQ_render_thumbnail_first_frame_get(scene, seq, frame_step, view_area);
  while ((timeline_frame < upper_thumb_bound) & !*stop) {
    ImBuf *ibuf = seq_cache_get(
        context, seq_orig, round_fl_to_int(timeline_frame), SEQ_CACHE_STORE_THUMBNAIL);
    if (ibuf) {
      IMB_freeImBuf(ibuf);
      timeline_frame = SEQ_render_thumbnail_next_frame_get(scene, seq, timeline_frame, frame_step);
      continue;
    }

    ibuf = seq_get_uncached_thumbnail(context, &state, seq, round_fl_to_int(timeline_frame));

    if (ibuf) {
      seq_cache_thumbnail_put(context, seq_orig, round_fl_to_int(timeline_frame), ibuf, view_area);
      IMB_freeImBuf(ibuf);
      seq_orig->flag &= ~SEQ_FLAG_SKIP_THUMBNAILS;
    }
    else {
      /* Can not open source file. */
      seq_orig->flag |= SEQ_FLAG_SKIP_THUMBNAILS;
      return;
    }

    timeline_frame = SEQ_render_thumbnail_next_frame_get(scene, seq, timeline_frame, frame_step);
  }
}

int SEQ_render_thumbnails_guaranteed_set_frame_step_get(const Scene *scene, const Sequence *seq)
{
  const int content_start = max_ii(SEQ_time_left_handle_frame_get(scene, seq), seq->start);
  const int content_end = min_ii(SEQ_time_right_handle_frame_get(scene, seq),
                                 seq->start + seq->len);
  const int content_len = content_end - content_start;

  /* Arbitrary, but due to performance reasons should be as low as possible. */
  const int thumbnails_base_set_count = min_ii(content_len / 100, 30);
  if (thumbnails_base_set_count <= 0) {
    return content_len;
  }
  return content_len / thumbnails_base_set_count;
}

void SEQ_render_thumbnails_base_set(const SeqRenderData *context,
                                    Sequence *seq,
                                    Sequence *seq_orig,
                                    rctf *view_area,
                                    const short *stop)
{
  SeqRenderState state;
  seq_render_state_init(&state);
  const Scene *scene = context->scene;

  int timeline_frame = SEQ_time_left_handle_frame_get(scene, seq);
  const int frame_step = SEQ_render_thumbnails_guaranteed_set_frame_step_get(scene, seq);

  while (timeline_frame < SEQ_time_right_handle_frame_get(scene, seq) && !*stop) {
    ImBuf *ibuf = seq_cache_get(
        context, seq_orig, roundf(timeline_frame), SEQ_CACHE_STORE_THUMBNAIL);
    if (ibuf) {
      IMB_freeImBuf(ibuf);

      if (frame_step == 0) {
        return;
      }

      timeline_frame += frame_step;
      continue;
    }

    ibuf = seq_get_uncached_thumbnail(context, &state, seq, timeline_frame);

    if (ibuf) {
      seq_cache_thumbnail_put(context, seq_orig, timeline_frame, ibuf, view_area);
      IMB_freeImBuf(ibuf);
    }

    if (frame_step == 0) {
      return;
    }

    timeline_frame += frame_step;
  }
}

bool SEQ_render_is_muted(const ListBase *channels, const Sequence *seq)
{

  SeqTimelineChannel *channel = SEQ_channel_get_by_index(channels, seq->machine);
  return seq->flag & SEQ_MUTE || SEQ_channel_is_muted(channel);
}

/** \} */
