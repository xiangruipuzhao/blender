# ##### BEGIN GPL LICENSE BLOCK #####
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software Foundation,
#  Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
#
# ##### END GPL LICENSE BLOCK #####

import bpy
from rigify import bone_class_instance, copy_bone_simple, add_pole_target_bone, add_stretch_to
from rna_prop_ui import rna_idprop_ui_get, rna_idprop_ui_prop_get

def metarig_template():
    # generated by rigify.write_meta_rig
    bpy.ops.object.mode_set(mode='EDIT')
    obj = bpy.context.object
    arm = obj.data
    bone = arm.edit_bones.new('hips')
    bone.head[:] = 0.0000, 0.0000, 0.0000
    bone.tail[:] = 0.0000, 0.0000, 1.0000
    bone.roll = 0.0000
    bone.connected = False
    bone = arm.edit_bones.new('thigh')
    bone.head[:] = 0.5000, 0.0000, 0.0000
    bone.tail[:] = 0.3000, -0.1000, -1.7000
    bone.roll = 0.1171
    bone.connected = False
    bone.parent = arm.edit_bones['hips']
    bone = arm.edit_bones.new('shin')
    bone.head[:] = 0.3000, -0.1000, -1.7000
    bone.tail[:] = 0.3000, 0.0000, -3.5000
    bone.roll = -0.0000
    bone.connected = True
    bone.parent = arm.edit_bones['thigh']
    bone = arm.edit_bones.new('foot')
    bone.head[:] = 0.3000, 0.0000, -3.5000
    bone.tail[:] = 0.4042, -0.5909, -3.9000
    bone.roll = -0.4662
    bone.connected = True
    bone.parent = arm.edit_bones['shin']
    bone = arm.edit_bones.new('toe')
    bone.head[:] = 0.4042, -0.5909, -3.9000
    bone.tail[:] = 0.4391, -0.9894, -3.9000
    bone.roll = -3.1416
    bone.connected = True
    bone.parent = arm.edit_bones['foot']
    bone = arm.edit_bones.new('heel')
    bone.head[:] = 0.2600, 0.2000, -4.0000
    bone.tail[:] = 0.3700, -0.4000, -4.0000
    bone.roll = 0.0000
    bone.connected = False
    bone.parent = arm.edit_bones['foot']

    bpy.ops.object.mode_set(mode='OBJECT')
    pbone = obj.pose.bones['thigh']
    pbone['type'] = 'leg'


def validate(obj, orig_bone_name):
    '''
    The bone given is the first in a chain
    Expects a chain of at least 3 children.
    eg.
        thigh -> shin -> foot -> [toe, heel]
    '''
    orig_bone = obj.data.bones[orig_bone_name]
    
    bone = orig_bone
    chain = 0
    while chain < 3: # first 2 bones only have 1 child
        children = bone.children
        if len(children) != 1:
            return "expected the thigh bone to have 3 children without a fork"
        bone = children[0]
        chain += 1
        
    children = bone.children
    # Now there must be 2 children, only one connected
    if len(children) != 2:
        return "expected the foot to have 2 children"

    if children[0].connected == children[1].connected:
        return "expected one bone to be connected"
    
    return ''


def main(obj, orig_bone_name):
    from Mathutils import Vector
    arm = obj.data
    
    # setup the existing bones
    mt_chain = bone_class_instance(obj, ["thigh", "shin", "foot", "toe"])
    mt = bone_class_instance(obj, ["hips", "heel"])
    #ex = bone_class_instance(obj, [""])
    ex = bone_class_instance(obj, ["thigh_socket", "foot_roll_1", "foot_roll_2", "foot_roll_3"])
    # children of ik_foot
    ik = bone_class_instance(obj, ["foot", "foot_roll", "foot_roll_01", "foot_roll_02", "knee_target"])
    
    mt_chain.thigh_e = arm.edit_bones[orig_bone_name]
    mt_chain.thigh = orig_bone_name
    
    mt.hips_e = mt_chain.thigh_e.parent
    mt.hips_e.name = "ORG-" + mt.hips_e.name
    mt.hips = mt.hips_e.name
    
    mt_chain.shin_e = mt_chain.thigh_e.children[0]
    mt_chain.shin = mt_chain.shin_e.name
    
    mt_chain.foot_e = mt_chain.shin_e.children[0]
    mt_chain.foot = mt_chain.foot_e.name
    
    mt_chain.toe_e, mt.heel_e = mt_chain.foot_e.children

    # We dont know which is which, but know the heel is disconnected
    if not mt_chain.toe_e.connected:
        mt_chain.toe_e, mt.heel_e = mt.heel_e, mt_chain.toe_e
    mt.heel_e.name = "ORG-" + mt.heel_e.name
    mt_chain.toe, mt.heel = mt_chain.toe_e.name, mt.heel_e.name

    ex.thigh_socket_e = copy_bone_simple(arm, mt_chain.thigh, "MCH-%s_socket" % mt_chain.thigh, parent=True)
    ex.thigh_socket = ex.thigh_socket_e.name
    ex.thigh_socket_e.tail = ex.thigh_socket_e.head + Vector(0.0, 0.0, ex.thigh_socket_e.length / 4.0)

    # Make a new chain, ORG are the original bones renamed.
    fk_chain = mt_chain.copy(from_prefix="ORG-") # fk has no prefix!
    ik_chain = fk_chain.copy(to_prefix="MCH-")

    # fk_chain.thigh_socket_e.parent = MCH-leg_hinge
    
    # simple rename
    fk_chain.thigh_e.name = fk_chain.thigh_e.name + "_ik"
    fk_chain.thigh = ik_chain.thigh_e.name
    
    fk_chain.shin_e.name = fk_chain.shin_e.name + "_ik"
    fk_chain.shin = ik_chain.shin_e.name
    
    
    # ik foot, no parents
    base_foot_name = ik_chain.foot # whatever the foot is called, use that!
    ik.foot_e = copy_bone_simple(arm, fk_chain.foot, "%s_ik" % base_foot_name)
    ik.foot = ik.foot_e.name
    ik.foot_e.tail.z = ik.foot_e.head.z
    ik.foot_e.roll = 0.0
    
    # heel pointing backwards, half length
    ik.foot_roll_e = copy_bone_simple(arm, mt.heel, "%s_roll" % base_foot_name)
    ik.foot_roll = ik.foot_roll_e.name
    ik.foot_roll_e.tail = ik.foot_roll_e.head + (ik.foot_roll_e.head - ik.foot_roll_e.tail) / 2.0
    ik.foot_roll_e.parent = ik.foot_e # heel is disconnected
    
    # heel pointing forwards to the toe base, parent of the following 2 bones
    ik.foot_roll_01_e = copy_bone_simple(arm, mt.heel, "MCH-%s_roll.01" % base_foot_name)
    ik.foot_roll_01 = ik.foot_roll_01_e.name
    ik.foot_roll_01_e.tail = mt_chain.foot_e.tail
    ik.foot_roll_01_e.parent = ik.foot_e # heel is disconnected

    # same as above but reverse direction
    ik.foot_roll_02_e = copy_bone_simple(arm, mt.heel, "MCH-%s_roll.02" % base_foot_name)
    ik.foot_roll_02 = ik.foot_roll_02_e.name
    ik.foot_roll_02_e.parent = ik.foot_roll_01_e # heel is disconnected
    ik.foot_roll_02_e.head = mt_chain.foot_e.tail
    ik.foot_roll_02_e.tail = mt.heel_e.head
    
    del base_foot_name
    
    # rename 'MCH-toe' --> to 'toe_ik' and make the child of ik.foot_roll_01
    fk_chain.toe_e.name = ik_chain.toe + "_ik"
    fk_chain.toe = fk_chain.toe_e.name
    fk_chain.toe_e.connected = True
    fk_chain.toe_e.parent = ik.foot_roll_01_e
    
    # re-parent ik_chain.foot to the 
    fk_chain.foot_e.connected = False
    fk_chain.foot_e.parent = ik.foot_roll_02_e
    
    
    # add remaining ik helper bones.
    
    # knee target is the heel moved up and forward on its local axis
    
    ik.knee_target_e = copy_bone_simple(arm, mt.heel, "knee_target")
    ik.knee_target = ik.knee_target_e.name
    offset = ik.knee_target_e.tail - ik.knee_target_e.head
    offset.z = 0
    offset.length = mt_chain.shin_e.head.z - mt.heel_e.head.z
    offset.z += offset.length
    ik.knee_target_e.translate(offset)
    ik.knee_target_e.length *= 0.5
    ik.knee_target_e.parent = ik.foot_e

    bpy.ops.object.mode_set(mode='OBJECT')

    ik.update()
    ex.update()
    mt_chain.update()
    ik_chain.update()
    fk_chain.update()

    con = fk_chain.thigh_p.constraints.new('COPY_LOCATION')
    con.target = obj
    con.subtarget = ex.thigh_socket
    
    
    # adds constraints to the original bones.
    mt_chain.blend(fk_chain, ik_chain, target_bone=ik.foot, target_prop="ik", use_loc=False)
