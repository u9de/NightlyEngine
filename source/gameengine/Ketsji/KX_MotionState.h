/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
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
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file KX_MotionState.h
 *  \ingroup ketsji
 */

#ifndef __KX_MOTIONSTATE_H__
#define __KX_MOTIONSTATE_H__

#include "PHY_IMotionState.h"

class SG_Node;

class KX_MotionState : public PHY_IMotionState {
  SG_Node *m_node;

 public:
  KX_MotionState(SG_Node *spatial);
  virtual ~KX_MotionState();

  virtual MT_Vector3 GetWorldPosition() const;
  virtual MT_Vector3 GetWorldScaling() const;
  virtual MT_Matrix3x3 GetWorldOrientation() const;

  virtual void SetWorldPosition(const MT_Vector3 &pos);
  virtual void SetWorldOrientation(const MT_Matrix3x3 &ori);
  virtual void SetWorldOrientation(const MT_Quaternion &quat);

  virtual void CalculateWorldTransformations();
};

#endif  // __KX_MOTIONSTATE_H__
