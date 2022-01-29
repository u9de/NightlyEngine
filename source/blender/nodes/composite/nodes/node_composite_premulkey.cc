/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2006 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup cmpnodes
 */

#include "UI_interface.h"
#include "UI_resources.h"

#include "node_composite_util.hh"

/* **************** Premul and Key Alpha Convert ******************** */

namespace blender::nodes::node_composite_premulkey_cc {

static void cmp_node_premulkey_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Color>(N_("Image")).default_value({1.0f, 1.0f, 1.0f, 1.0f});
  b.add_output<decl::Color>(N_("Image"));
}

static void node_composit_buts_premulkey(uiLayout *layout, bContext *UNUSED(C), PointerRNA *ptr)
{
  uiItemR(layout, ptr, "mapping", UI_ITEM_R_SPLIT_EMPTY_NAME, "", ICON_NONE);
}

}  // namespace blender::nodes::node_composite_premulkey_cc

void register_node_type_cmp_premulkey()
{
  namespace file_ns = blender::nodes::node_composite_premulkey_cc;

  static bNodeType ntype;

  cmp_node_type_base(&ntype, CMP_NODE_PREMULKEY, "Alpha Convert", NODE_CLASS_CONVERTER);
  ntype.declare = file_ns::cmp_node_premulkey_declare;
  ntype.draw_buttons = file_ns::node_composit_buts_premulkey;

  nodeRegisterType(&ntype);
}