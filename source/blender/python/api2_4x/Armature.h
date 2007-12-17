/* 
 * $Id: Armature.h 12898 2007-12-15 21:44:40Z campbellbarton $
 *
 * ***** BEGIN GPL/BL DUAL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version. The Blender
 * Foundation also sells licenses for use in proprietary software under
 * the Blender License.  See http://www.blender.org/BL/ for information
 * about this.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 *
 * Contributor(s): Joseph gilbert
 *
 * ***** END GPL/BL DUAL LICENSE BLOCK *****
*/

#ifndef V24_EXPP_ARMATURE_H
#define V24_EXPP_ARMATURE_H

#include <Python.h>
#include "DNA_armature_types.h"

//-------------------TYPE CHECKS---------------------------------
#define V24_BPy_Armature_Check(v) ((v)->ob_type == &V24_Armature_Type)
#define V24_BPy_BonesDict_Check(v) ((v)->ob_type == &V24_BonesDict_Type)
//-------------------MODULE INIT---------------------------------
PyObject *V24_Armature_Init( void );
//-------------------TYPEOBJECT----------------------------------
extern PyTypeObject V24_Armature_Type;
extern PyTypeObject V24_BonesDict_Type;
//-------------------STRUCT DEFINITION---------------------------
typedef struct {
	PyObject_HEAD 
	PyObject *bonesMap;      //wrapper for bones
	PyObject *editbonesMap; //wrapper for editbones
	ListBase *bones;            //pointer to armature->bonebase
	ListBase editbones;         //allocated list of EditBones 
	short editmode_flag;       //1 = in , 0 = not in
} V24_BPy_BonesDict;

typedef struct {
	PyObject_HEAD 
	struct bArmature * armature;
	V24_BPy_BonesDict *Bones;          //V24_BPy_BonesDict
	PyObject *weaklist;
} V24_BPy_Armature;

//-------------------VISIBLE PROTOTYPES-------------------------
PyObject *V24_Armature_CreatePyObject(struct bArmature *armature);
struct bArmature *V24_PyArmature_AsArmature(V24_BPy_Armature *py_armature);
PyObject * V24_Armature_RebuildEditbones(PyObject *pyarmature);
PyObject *V24_Armature_RebuildBones(PyObject *pyarmature);

struct bArmature  *V24_Armature_FromPyObject( PyObject * py_obj );

#endif				
