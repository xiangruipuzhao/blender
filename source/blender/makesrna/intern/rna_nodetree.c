/**
 * $Id$
 *
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
 * Contributor(s): Blender Foundation (2008), Nathan Letwory, Robin Allen, Bob Holcomb
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#include <stdlib.h>
#include <string.h>

#include "RNA_define.h"

#include "rna_internal.h"

#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include "DNA_scene_types.h"
#include "DNA_texture_types.h"

#include "BKE_animsys.h"
#include "BKE_main.h"
#include "BKE_node.h"
#include "BKE_image.h"
#include "BKE_texture.h"

#include "BLI_math.h"

#include "WM_types.h"

#include "MEM_guardedalloc.h"

#ifdef RNA_RUNTIME

#include "BLI_linklist.h"

#include "ED_node.h"

#include "RE_pipeline.h"

#include "DNA_scene_types.h"
#include "WM_api.h"

static StructRNA *rna_Node_refine(struct PointerRNA *ptr)
{
	bNode *node = (bNode*)ptr->data;

	switch(node->type) {
		
		#define DefNode(Category, ID, DefFunc, EnumName, StructName, UIName, UIDesc) \
			case ID: return &RNA_##Category##StructName;
				
		#include "rna_nodetree_types.h"
		
		#undef DefNode
		
		case NODE_GROUP:
			return &RNA_NodeGroup;
			
		default:
			return &RNA_Node;
	}
}

static StructRNA *rna_NodeSocketType_refine(struct PointerRNA *ptr)
{
	bNodeSocket *ns= (bNodeSocket*)ptr->data;
	
	switch(ns->type) {
		case SOCK_VALUE:
			return &RNA_ValueNodeSocket;
		case SOCK_VECTOR:
			return &RNA_VectorNodeSocket;
		case SOCK_RGBA:
			return &RNA_RGBANodeSocket;
		default:
			return &RNA_UnknownType;
	}
}		

static StructRNA *rna_NodeTree_refine(struct PointerRNA *ptr)
{
	bNodeTree *ntree= (bNodeTree*)ptr->data;

	switch(ntree->type) {
		case NTREE_SHADER:
			return &RNA_ShaderNodeTree;
		case NTREE_COMPOSIT:
			return &RNA_CompositorNodeTree;
		case NTREE_TEXTURE:
			return &RNA_TextureNodeTree;
		default:
			return &RNA_UnknownType;
	}
}

static char *rna_Node_path(PointerRNA *ptr)
{
	bNode *node= (bNode*)ptr->data;

	return BLI_sprintfN("nodes[\"%s\"]", node->name);
}

static char *rna_NodeSocket_path(PointerRNA *ptr)
{
	bNodeTree *ntree= (bNodeTree*)ptr->id.data;
	bNodeSocket *sock= (bNodeSocket*)ptr->data;
	bNode *node;
	int socketindex;
	
	if (!nodeFindNode(ntree, sock, &node, NULL)) return NULL;

	socketindex = BLI_findindex(&node->inputs, sock);
	if (socketindex != -1)
		return BLI_sprintfN("nodes[\"%s\"].inputs[%d]", node->name, socketindex);

	socketindex = BLI_findindex(&node->outputs, sock);
	if (socketindex != -1)
		return BLI_sprintfN("nodes[\"%s\"].outputs[%d]", node->name, socketindex);
	
	return NULL;
}

/* Button Set Funcs for Matte Nodes */
static void rna_Matte_t1_set(PointerRNA *ptr, float value)
{
	bNode *node= (bNode*)ptr->data;
	NodeChroma *chroma = node->storage;
	
	chroma->t1 = value;
	
	if(value < chroma->t2) 
		chroma->t2 = value;
}

static void rna_Matte_t2_set(PointerRNA *ptr, float value)
{
	bNode *node= (bNode*)ptr->data;
	NodeChroma *chroma = node->storage;
	
	if(value > chroma->t1) 
		value = chroma->t1;
	
	chroma->t2 = value;
}

static void rna_Image_start_frame_set(PointerRNA *ptr, int value)
{
	bNode *node= (bNode*)ptr->data;
	NodeImageFile *image = node->storage;
	
	CLAMP(value, MINFRAME, image->efra); 
	image->sfra= value;
}

static void rna_Image_end_frame_set(PointerRNA *ptr, int value)
{
	bNode *node= (bNode*)ptr->data;
	NodeImageFile *image = node->storage;

	CLAMP(value, image->sfra, MAXFRAME);
	image->efra= value;
}

static void rna_Node_scene_set(PointerRNA *ptr, PointerRNA value)
{
	bNode *node= (bNode*)ptr->data;

	if (node->id) {
		id_us_min(node->id);
		node->id= NULL;
	}

	node->id= value.data;

	id_us_plus(node->id);
}



static void node_update(Main *bmain, Scene *scene, bNodeTree *ntree, bNode *node)
{
	ED_node_generic_update(bmain, ntree, node);
}

static void rna_Node_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
	bNodeTree *ntree= (bNodeTree*)ptr->id.data;
	bNode *node= (bNode*)ptr->data;

	node_update(bmain, scene, ntree, node);
}

static void rna_NodeGroup_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
	bNodeTree *ntree= (bNodeTree*)ptr->id.data;
	bNode *node= (bNode*)ptr->data;
	
	nodeVerifyGroup((bNodeTree *)node->id);
	
	node_update(bmain, scene, ntree, node);
}

static void rna_Node_name_set(PointerRNA *ptr, const char *value)
{
	bNodeTree *ntree= (bNodeTree*)ptr->id.data;
	bNode *node= (bNode*)ptr->data;
	char oldname[sizeof(node->name)];
	
	/* make a copy of the old name first */
	BLI_strncpy(oldname, node->name, sizeof(node->name));
	/* set new name */
	BLI_strncpy(node->name, value, sizeof(node->name));
	
	nodeUniqueName(ntree, node);
	node->flag |= NODE_CUSTOM_NAME;
	
	/* fix all the animation data which may link to this */
	BKE_all_animdata_fix_paths_rename("nodes", oldname, node->name);
}

/* this should be done at display time! if no custom names are set */
#if 0
static void rna_Node_update_username(Main *bmain, Scene *scene, PointerRNA *ptr)
{
	bNode *node= (bNode*)ptr->data;
	const char *name;

	
	/*
	if (!node->username[0]) {
		if(node->id) {
			BLI_strncpy(node->username, node->id->name+2, NODE_MAXSTR);
		}
		else {
		
			switch(node->typeinfo->type) {
				case SH_NODE_MIX_RGB:
				case CMP_NODE_MIX_RGB:
				case TEX_NODE_MIX_RGB:
					if(RNA_enum_name(node_blend_type_items, node->custom1, &name))
						BLI_strncpy(node->username, name, NODE_MAXSTR);
					break;
				case CMP_NODE_FILTER:
					if(RNA_enum_name(node_filter_items, node->custom1, &name))
						BLI_strncpy(node->username, name, NODE_MAXSTR);
					break;
				case CMP_NODE_FLIP:
					if(RNA_enum_name(node_flip_items, node->custom1, &name))
						BLI_strncpy(node->username, name, NODE_MAXSTR);
					break;
				case SH_NODE_MATH:
				case CMP_NODE_MATH:
				case TEX_NODE_MATH:
					if(RNA_enum_name(node_math_items, node->custom1, &name))
						BLI_strncpy(node->username, name, NODE_MAXSTR);
					break;
				case SH_NODE_VECT_MATH:
					if(RNA_enum_name(node_vec_math_items, node->custom1, &name))
						BLI_strncpy(node->username, name, NODE_MAXSTR);
					break;
			}
		 */
		}
	}

	rna_Node_update(bmain, scene, ptr);
}
#endif

static void rna_NodeSocket_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
	bNodeTree *ntree= (bNodeTree*)ptr->id.data;
	bNodeSocket *sock= (bNodeSocket*)ptr->data;
	bNode *node;
	
	if (nodeFindNode(ntree, sock, &node, NULL))
		node_update(bmain, scene, ntree, node);
}

static void rna_NodeSocket_defvalue_range(PointerRNA *ptr, float *min, float *max)
{
	bNodeSocket *sock= (bNodeSocket*)ptr->data;

	*min = sock->ns.min;
	*max = sock->ns.max;
}

static void rna_Node_mapping_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
	bNode *node= (bNode*)ptr->data;

	init_mapping((TexMapping *)node->storage);
	
	rna_Node_update(bmain, scene, ptr);
}

static void rna_Node_image_layer_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
	bNode *node= (bNode*)ptr->data;
	Image *ima = (Image *)node->id;
	ImageUser *iuser= node->storage;
	
	BKE_image_multilayer_index(ima->rr, iuser);
	/* do not call below function, it frees the multilayer exr */
	/* BKE_image_signal(ima, iuser, IMA_SIGNAL_SRC_CHANGE); */
	
	rna_Node_update(bmain, scene, ptr);
}

static EnumPropertyItem *renderresult_layers_add_enum(RenderLayer *rl)
{
	EnumPropertyItem *item= NULL;
	EnumPropertyItem tmp = {0, "", 0, "", ""};
	int i=0, totitem=0;
	
	while (rl) {
		tmp.identifier = rl->name;
		tmp.name= rl->name;
		tmp.value = i++;
		RNA_enum_item_add(&item, &totitem, &tmp);
		rl=rl->next;
	}
	
	RNA_enum_item_end(&item, &totitem);

	return item;
}

static EnumPropertyItem *rna_Node_image_layer_itemf(bContext *C, PointerRNA *ptr, int *free)
{
	bNode *node= (bNode*)ptr->data;
	Image *ima = (Image *)node->id;
	EnumPropertyItem *item= NULL;
	RenderLayer *rl;
	
	if (!ima || !(ima->rr)) return NULL;

	rl = ima->rr->layers.first;
	item = renderresult_layers_add_enum(rl);
	
	*free= 1;
	
	return item;
}

static EnumPropertyItem *rna_Node_scene_layer_itemf(bContext *C, PointerRNA *ptr, int *free)
{
	bNode *node= (bNode*)ptr->data;
	Scene *sce = (Scene *)node->id;
	EnumPropertyItem *item= NULL;
	RenderLayer *rl;
	
	if (!sce) return NULL;
	
	rl = sce->r.layers.first;
	item = renderresult_layers_add_enum(rl);
	
	*free= 1;
	
	return item;
}

static EnumPropertyItem *rna_Node_channel_itemf(bContext *C, PointerRNA *ptr, int *free)
{
	bNode *node= (bNode*)ptr->data;
	EnumPropertyItem *item= NULL;
	EnumPropertyItem tmp = {0, "", 0, "", ""};
	int totitem=0;
	
	switch(node->custom1) {
		case CMP_NODE_CHANNEL_MATTE_CS_RGB:
			tmp.identifier= "R"; tmp.name= "R"; tmp.value= 1;
			RNA_enum_item_add(&item, &totitem, &tmp);
			tmp.identifier= "G"; tmp.name= "G"; tmp.value= 2;
			RNA_enum_item_add(&item, &totitem, &tmp);
			tmp.identifier= "B"; tmp.name= "B"; tmp.value= 3;
			RNA_enum_item_add(&item, &totitem, &tmp);
			break;
		case CMP_NODE_CHANNEL_MATTE_CS_HSV:
			tmp.identifier= "H"; tmp.name= "H"; tmp.value= 1;
			RNA_enum_item_add(&item, &totitem, &tmp);
			tmp.identifier= "S"; tmp.name= "S"; tmp.value= 2;
			RNA_enum_item_add(&item, &totitem, &tmp);
			tmp.identifier= "V"; tmp.name= "V"; tmp.value= 3;
			RNA_enum_item_add(&item, &totitem, &tmp);
			break;
		case CMP_NODE_CHANNEL_MATTE_CS_YUV:
			tmp.identifier= "Y"; tmp.name= "Y"; tmp.value= 1;
			RNA_enum_item_add(&item, &totitem, &tmp);
			tmp.identifier= "G"; tmp.name= "U"; tmp.value= 2;
			RNA_enum_item_add(&item, &totitem, &tmp);
			tmp.identifier= "V"; tmp.name= "V"; tmp.value= 3;
			RNA_enum_item_add(&item, &totitem, &tmp);
			break;
		case CMP_NODE_CHANNEL_MATTE_CS_YCC:
			tmp.identifier= "Y"; tmp.name= "Y"; tmp.value= 1;
			RNA_enum_item_add(&item, &totitem, &tmp);
			tmp.identifier= "CB"; tmp.name= "Cr"; tmp.value= 2;
			RNA_enum_item_add(&item, &totitem, &tmp);
			tmp.identifier= "CR"; tmp.name= "Cb"; tmp.value= 3;
			RNA_enum_item_add(&item, &totitem, &tmp);
			break;
		default:
			break;
	}

	RNA_enum_item_end(&item, &totitem);
	*free= 1;
	
	return item;
}

static bNode *rna_NodeTree_node_new(bNodeTree *ntree, bContext *C, ReportList *reports, int type, bNodeTree *group)
{
	bNode *node;

	if (type == NODE_GROUP && group == NULL) {
		BKE_reportf(reports, RPT_ERROR, "node type \'GROUP\' missing group argument");
		return NULL;
	}
	node = nodeAddNodeType(ntree, type, group, NULL);

	if (node == NULL) {
		 BKE_reportf(reports, RPT_ERROR, "Unable to create node");
	}
	else {
		nodeVerifyGroup(ntree); /* update group node socket links*/
		NodeTagChanged(ntree, node);
		WM_main_add_notifier(NC_NODE|NA_EDITED, ntree);

		if (group)
			id_us_plus(&group->id);
	}

	return node;
}

static bNode *rna_NodeTree_node_composite_new(bNodeTree *ntree, bContext *C, ReportList *reports, int type, bNodeTree *group)
{
	/* raises error on failier */
	bNode *node= rna_NodeTree_node_new(ntree, C, reports, type, group);
	
	if (node) {
		if(ELEM4(node->type, CMP_NODE_COMPOSITE, CMP_NODE_DEFOCUS, CMP_NODE_OUTPUT_FILE, CMP_NODE_R_LAYERS)) {
			/* annoying, find the node tree we are in, scene can be NULL */
			Scene *scene;
			for(scene= CTX_data_main(C)->scene.first; scene; scene= scene->id.next) {
				if(scene->nodetree == ntree) {
					break;
				}
			}
			node->id= (ID *)scene;
			id_us_plus(node->id);
		}

		ntreeCompositForceHidden(ntree, CTX_data_scene(C));
		ntreeSolveOrder(ntree);
	}

	return node;
}

static bNode *rna_NodeTree_node_texture_new(bNodeTree *ntree, bContext *C, ReportList *reports, int type, bNodeTree *group)
{
	/* raises error on failier */
	bNode *node= rna_NodeTree_node_new(ntree, C, reports, type, group);

	if (node) {
		ntreeTexCheckCyclics(ntree);
	}

	return node;
}

static void rna_NodeTree_node_remove(bNodeTree *ntree, ReportList *reports, bNode *node)
{
	if (BLI_findindex(&ntree->nodes, node) == -1) {
		BKE_reportf(reports, RPT_ERROR, "Unable to locate node '%s' in nodetree", node->name);
	}
	else {
		if (node->id)
			id_us_min(node->id);

		nodeFreeNode(ntree, node);
		nodeVerifyGroup(ntree); /* update group node socket links*/

		WM_main_add_notifier(NC_NODE|NA_EDITED, ntree);
	}
}

static bNodeLink *rna_NodeTree_link_new(bNodeTree *ntree, ReportList *reports, bNodeSocket *in, bNodeSocket *out)
{
	bNodeLink *ret;
	bNode *fromnode, *tonode;

	if (!nodeFindNode(ntree, in, &fromnode, NULL)) {
		BKE_reportf(reports, RPT_ERROR, "Unable to locate input socket's node in nodetree");
		return NULL;
	}

	if (!nodeFindNode(ntree, out, &tonode, NULL)) {
		BKE_reportf(reports, RPT_ERROR, "Unable to locate output socket's node in nodetree");
		return NULL;
	}

	/* unlink node input socket */
	nodeRemSocketLinks(ntree, out);

	ret= nodeAddLink(ntree, fromnode, in, tonode, out);

	NodeTagChanged(ntree, tonode);

	ntreeSolveOrder(ntree);

	WM_main_add_notifier(NC_NODE|NA_EDITED, ntree);

	return ret;
}

static void rna_NodeTree_link_remove(bNodeTree *ntree, ReportList *reports, bNodeLink *link)
{
	if (BLI_findindex(&ntree->links, link) == -1) {
		BKE_reportf(reports, RPT_ERROR, "Unable to locate link in nodetree");
	}
	else {
		nodeRemLink(ntree, link);
		ntreeSolveOrder(ntree);
		nodeVerifyGroup(ntree); /* update group node socket links*/

		WM_main_add_notifier(NC_NODE|NA_EDITED, ntree);
	}
}

#else

static EnumPropertyItem prop_image_layer_items[] = {
{ 0, "PLACEHOLDER",          0, "Placeholder",          ""},
{0, NULL, 0, NULL, NULL}};

static EnumPropertyItem prop_scene_layer_items[] = {
{ 0, "PLACEHOLDER",          0, "Placeholder",          ""},
{0, NULL, 0, NULL, NULL}};

static EnumPropertyItem prop_tri_channel_items[] = {
{ 1, "R", 0, "R", ""},
{ 2, "G", 0, "G", ""},
{ 3, "B", 0, "B", ""},
{0, NULL, 0, NULL, NULL}};

static EnumPropertyItem node_blend_type_items[] = {
{ 0, "MIX",          0, "Mix",         ""},
{ 1, "ADD",          0, "Add",         ""},
{ 3, "SUBTRACT",     0, "Subtract",    ""},
{ 2, "MULTIPLY",     0, "Multiply",    ""},
{ 4, "SCREEN",       0, "Screen",      ""},
{ 9, "OVERLAY",      0, "Overlay",     ""},
{ 5, "DIVIDE",       0, "Divide",      ""},
{ 6, "DIFFERENCE",   0, "Difference",  ""},
{ 7, "DARKEN",       0, "Darken",      ""},
{ 8, "LIGHTEN",      0, "Lighten",     ""},
{10, "DODGE",        0, "Dodge",       ""},
{11, "BURN",         0, "Burn",        ""},
{15, "COLOR",        0, "Color",       ""},
{14, "VALUE",        0, "Value",       ""},
{13, "SATURATION",   0, "Saturation",  ""},
{12, "HUE",          0, "Hue",         ""},
{16, "SOFT_LIGHT",   0, "Soft Light",  ""},
{17, "LINEAR_LIGHT", 0, "Linear Light",""},
{0, NULL, 0, NULL, NULL}};

static EnumPropertyItem node_flip_items[] = {
{0, "X",  0, "Flip X",     ""},
{1, "Y",  0, "Flip Y",     ""},
{2, "XY", 0, "Flip X & Y", ""},
{0, NULL, 0, NULL, NULL}};

static EnumPropertyItem node_math_items[] = {
{ 0, "ADD",          0, "Add",          ""},
{ 1, "SUBTRACT",     0, "Subtract",     ""},
{ 2, "MULTIPLY",     0, "Multiply",     ""},
{ 3, "DIVIDE",       0, "Divide",       ""},
{ 4, "SINE",         0, "Sine",         ""},
{ 5, "COSINE",       0, "Cosine",       ""},
{ 6, "TANGENT",      0, "Tangent",      ""},
{ 7, "ARCSINE",      0, "Arcsine",      ""},
{ 8, "ARCCOSINE",    0, "Arccosine",    ""},
{ 9, "ARCTANGENT",   0, "Arctangent",   ""},
{10, "POWER",        0, "Power",        ""},
{11, "LOGARITHM",    0, "Logarithm",    ""},
{12, "MINIMUM",      0, "Minimum",      ""},
{13, "MAXIMUM",      0, "Maximum",      ""},
{14, "ROUND",        0, "Round",        ""},
{15, "LESS_THAN",    0, "Less Than",    ""},
{16, "GREATER_THAN", 0, "Greater Than", ""},
{0, NULL, 0, NULL, NULL}};

static EnumPropertyItem node_vec_math_items[] = {
{0, "ADD",           0, "Add",           ""},
{1, "SUBTRACT",      0, "Subtract",      ""},
{2, "AVERAGE",       0, "Average",       ""},
{3, "DOT_PRODUCT",   0, "Dot Product",   ""},
{4, "CROSS_PRODUCT", 0, "Cross Product", ""},
{5, "NORMALIZE",     0, "Normalize",     ""},
{0, NULL, 0, NULL, NULL}};

static EnumPropertyItem node_filter_items[] = {
{0, "SOFTEN",  0, "Soften",  ""},
{1, "SHARPEN", 0, "Sharpen", ""},
{2, "LAPLACE", 0, "Laplace", ""},
{3, "SOBEL",   0, "Sobel",   ""},
{4, "PREWITT", 0, "Prewitt", ""},
{5, "KIRSCH",  0, "Kirsch",  ""},
{6, "SHADOW",  0, "Shadow",  ""},
{0, NULL, 0, NULL, NULL}};


#define MaxNodes 1000

enum
{
	Category_GroupNode,
	Category_ShaderNode,
	Category_CompositorNode,
	Category_TextureNode
};

typedef struct NodeInfo
{
	int defined;
	int category;
	const char *enum_name;
	const char *struct_name;
	const char *base_name;
	int icon;
	const char *ui_name;
	const char *ui_desc;
} NodeInfo;

static NodeInfo nodes[MaxNodes];

static void reg_node(int ID, int category, const char *enum_name, const char *struct_name,
					 const char *base_name, const char *ui_name, const char *ui_desc)
{
	NodeInfo *ni = nodes + ID;
	
	ni->defined = 1;
	ni->category = category;
	ni->enum_name = enum_name;
	ni->struct_name = struct_name;
	ni->base_name = base_name;
	ni->ui_name = ui_name;
	ni->ui_desc = ui_desc;
}

static void init(void)
{
	memset(nodes, 0, sizeof nodes);
	
	#define DefNode(Category, ID, DefFunc, EnumName, StructName, UIName, UIDesc) \
		reg_node(ID, Category_##Category, EnumName, STRINGIFY_ARG(Category##StructName), #Category, UIName, UIDesc);
		
	#include "rna_nodetree_types.h"
	
	#undef DefNode
	
	reg_node(NODE_GROUP, Category_GroupNode, "GROUP", "NodeGroup", "Node", "Group", "");
}

static StructRNA* def_node(BlenderRNA *brna, int node_id)
{
	StructRNA *srna;
	NodeInfo *node = nodes + node_id;
	
	srna = RNA_def_struct(brna, node->struct_name, node->base_name);
	RNA_def_struct_ui_text(srna, node->ui_name, node->ui_desc);
	RNA_def_struct_sdna(srna, "bNode");
	
	return srna;
}

void alloc_node_type_items(EnumPropertyItem *items, int category)
{
	int i;
	int count = 3;
	EnumPropertyItem *item  = items;
	
	for(i=0; i<MaxNodes; i++)
		if(nodes[i].defined && nodes[i].category == category)
			count++;
		
	/*item = items = MEM_callocN(count * sizeof(EnumPropertyItem), "alloc_node_type_items");*/
	
	for(i=0; i<MaxNodes; i++) {
		NodeInfo *node = nodes + i;
		if(node->defined && node->category == category) {
			item->value = i;
			item->identifier = node->enum_name;
			item->icon = node->icon;
			item->name = node->ui_name;
			item->description = node->ui_desc;
		
			item++;
		}
	}
	
	item->value = NODE_DYNAMIC;
	item->identifier = "SCRIPT";
	item->icon = 0;
	item->name = "Script";
	item->description = "";
	
	item++;
	
	item->value = NODE_GROUP;
	item->identifier = "GROUP";
	item->icon = 0;
	item->name = "Group";
	item->description = "";
	
	item++;
	
	/* NOTE!, increase 'count' when adding items here */
	
	memset(item, 0, sizeof(EnumPropertyItem));
}


/* -- Common nodes ---------------------------------------------------------- */

static void def_group(StructRNA *srna)
{
	PropertyRNA *prop;
	
	prop = RNA_def_property(srna, "node_tree", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "id");
	RNA_def_property_struct_type(prop, "NodeTree");
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Node Tree", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_NodeGroup_update");
}


static void def_math(StructRNA *srna)
{
	PropertyRNA *prop;
	
	prop = RNA_def_property(srna, "operation", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "custom1");
	RNA_def_property_enum_items(prop, node_math_items);
	RNA_def_property_ui_text(prop, "Operation", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_vector_math(StructRNA *srna)
{
	PropertyRNA *prop;
	
	prop = RNA_def_property(srna, "operation", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "custom1");
	RNA_def_property_enum_items(prop, node_vec_math_items);
	RNA_def_property_ui_text(prop, "Operation", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_rgb_curve(StructRNA *srna)
{
	PropertyRNA *prop;
	
	prop = RNA_def_property(srna, "mapping", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "storage");
	RNA_def_property_struct_type(prop, "CurveMapping");
	RNA_def_property_ui_text(prop, "Mapping", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_vector_curve(StructRNA *srna)
{
	PropertyRNA *prop;
	
	prop = RNA_def_property(srna, "mapping", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "storage");
	RNA_def_property_struct_type(prop, "CurveMapping");
	RNA_def_property_ui_text(prop, "Mapping", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_time(StructRNA *srna)
{
	PropertyRNA *prop;
	
	prop = RNA_def_property(srna, "curve", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "storage");
	RNA_def_property_struct_type(prop, "CurveMapping");
	RNA_def_property_ui_text(prop, "Curve", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "frame_start", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "custom1");
	RNA_def_property_ui_text(prop, "Start Frame", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "frame_end", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "custom2");
	RNA_def_property_ui_text(prop, "End Frame", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_colorramp(StructRNA *srna)
{
	PropertyRNA *prop;
	
	prop = RNA_def_property(srna, "color_ramp", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "storage");
	RNA_def_property_struct_type(prop, "ColorRamp");
	RNA_def_property_ui_text(prop, "Color Ramp", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_mix_rgb(StructRNA *srna)
{
	PropertyRNA *prop;
	
	prop = RNA_def_property(srna, "blend_type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "custom1");
	RNA_def_property_enum_items(prop, node_blend_type_items);
	RNA_def_property_ui_text(prop, "Blend Type", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "use_alpha", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "custom2", 1);
	RNA_def_property_ui_text(prop, "Alpha", "Include alpha of second input in this operation");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_texture(StructRNA *srna)
{
	PropertyRNA *prop;
	
	prop = RNA_def_property(srna, "texture", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "id");
	RNA_def_property_struct_type(prop, "Texture");
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Texture", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "node_output", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "custom1");
	RNA_def_property_ui_text(prop, "Node Output", "For node-based textures, which output node to use");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}


/* -- Shader Nodes ---------------------------------------------------------- */

static void def_sh_material(StructRNA *srna)
{
	PropertyRNA *prop;

	prop = RNA_def_property(srna, "material", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "id");
	RNA_def_property_struct_type(prop, "Material");
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Material", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");

	prop = RNA_def_property(srna, "use_diffuse", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "custom1", SH_NODE_MAT_DIFF);
	RNA_def_property_ui_text(prop, "Diffuse", "Material Node outputs Diffuse");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");

	prop = RNA_def_property(srna, "use_specular", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "custom1", SH_NODE_MAT_SPEC);
	RNA_def_property_ui_text(prop, "Specular", "Material Node outputs Specular");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "invert_normal", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "custom1", SH_NODE_MAT_NEG);
	RNA_def_property_ui_text(prop, "Invert Normal", "Material Node uses inverted normal");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_sh_mapping(StructRNA *srna)
{
	PropertyRNA *prop;
	
	RNA_def_struct_sdna_from(srna, "TexMapping", "storage");

	prop= RNA_def_property(srna, "location", PROP_FLOAT, PROP_TRANSLATION);
	RNA_def_property_float_sdna(prop, NULL, "loc");
	RNA_def_property_ui_text(prop, "Location", "Location offset for the input coordinate");
	RNA_def_property_ui_range(prop, -10.f, 10.f, 0.1f, 2);
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_mapping_update");
	
	prop= RNA_def_property(srna, "rotation", PROP_FLOAT, PROP_EULER);
	RNA_def_property_float_sdna(prop, NULL, "rot");
	RNA_def_property_ui_text(prop, "Rotation", "Rotation offset for the input coordinate");
	RNA_def_property_ui_range(prop, -360.f, 360.f, 1.f, 2);
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_mapping_update");
	
	prop= RNA_def_property(srna, "scale", PROP_FLOAT, PROP_XYZ);
	RNA_def_property_float_sdna(prop, NULL, "size");
	RNA_def_property_ui_text(prop, "Scale", "Scale adjustment for the input coordinate");
	RNA_def_property_ui_range(prop, -10.f, 10.f, 0.1f, 2);
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_mapping_update");
	
	prop = RNA_def_property(srna, "use_min", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", TEXMAP_CLIP_MIN);
	RNA_def_property_ui_text(prop, "Clamp Minimum", "Clamp the output coordinate to a minimum value");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop= RNA_def_property(srna, "min", PROP_FLOAT, PROP_XYZ);
	RNA_def_property_float_sdna(prop, NULL, "min");
	RNA_def_property_ui_text(prop, "Minimum", "Minimum value to clamp coordinate to");
	RNA_def_property_ui_range(prop, -10.f, 10.f, 0.1f, 2);
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "use_max", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", TEXMAP_CLIP_MAX);
	RNA_def_property_ui_text(prop, "Clamp Maximum", "Clamp the output coordinate to a maximum value");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop= RNA_def_property(srna, "max", PROP_FLOAT, PROP_XYZ);
	RNA_def_property_float_sdna(prop, NULL, "max");
	RNA_def_property_ui_text(prop, "Maximum", "Maximum value to clamp coordinate to");
	RNA_def_property_ui_range(prop, -10.f, 10.f, 0.1f, 2);
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_sh_geometry(StructRNA *srna)
{
	PropertyRNA *prop;
	
	RNA_def_struct_sdna_from(srna, "NodeGeometry", "storage");
	
	prop = RNA_def_property(srna, "uv_layer", PROP_STRING, PROP_NONE);
	RNA_def_property_string_sdna(prop, NULL, "uvname");
	RNA_def_property_ui_text(prop, "UV Layer", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "color_layer", PROP_STRING, PROP_NONE);
	RNA_def_property_string_sdna(prop, NULL, "colname");
	RNA_def_property_ui_text(prop, "Vertex Color Layer", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}


/* -- Compositor Nodes ------------------------------------------------------ */

static void def_cmp_alpha_over(StructRNA *srna)
{
	PropertyRNA *prop;
	
	// XXX: Tooltip
	prop = RNA_def_property(srna, "use_premultiply", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "custom1", 1);
	RNA_def_property_ui_text(prop, "Convert Premul", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	RNA_def_struct_sdna_from(srna, "NodeTwoFloats", "storage");
	
	prop = RNA_def_property(srna, "premul", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "x");
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Premul", "Mix Factor");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_cmp_hue_saturation(StructRNA *srna)
{
	PropertyRNA *prop;

	RNA_def_struct_sdna_from(srna, "NodeHueSat", "storage");
	
	prop = RNA_def_property(srna, "color_hue", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "hue");
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Hue", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "color_saturation", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "sat");
	RNA_def_property_range(prop, 0.0f, 2.0f);
	RNA_def_property_ui_text(prop, "Saturation", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "color_value", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "val");
	RNA_def_property_range(prop, 0.0f, 2.0f);
	RNA_def_property_ui_text(prop, "Value", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_cmp_blur(StructRNA *srna)
{
	PropertyRNA *prop;
	
	static EnumPropertyItem filter_type_items[] = {
		{R_FILTER_BOX,        "FLAT",       0, "Flat",          ""},
		{R_FILTER_TENT,       "TENT",       0, "Tent",          ""},
		{R_FILTER_QUAD,       "QUAD",       0, "Quadratic",     ""},
		{R_FILTER_CUBIC,      "CUBIC",      0, "Cubic",         ""},
		{R_FILTER_GAUSS,      "GAUSS",      0, "Gaussian",      ""},
		{R_FILTER_FAST_GAUSS, "FAST_GAUSS", 0, "Fast Gaussian", ""},
		{R_FILTER_CATROM,     "CATROM",     0, "Catrom",        ""},
		{R_FILTER_MITCH,      "MITCH",      0, "Mitch",         ""},
		{0, NULL, 0, NULL, NULL}};

	RNA_def_struct_sdna_from(srna, "NodeBlurData", "storage");
	
	prop = RNA_def_property(srna, "size_x", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "sizex");
	RNA_def_property_range(prop, 0, 2048);
	RNA_def_property_ui_text(prop, "Size X", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "size_y", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "sizey");
	RNA_def_property_range(prop, 0, 2048);
	RNA_def_property_ui_text(prop, "Size Y", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");

	prop = RNA_def_property(srna, "use_relative", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "relative", 1);
	RNA_def_property_ui_text(prop, "Relative", "Use relative (percent) values to define blur radius");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "factor", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "fac");
	RNA_def_property_range(prop, 0.0f, 2.0f);
	RNA_def_property_ui_text(prop, "Factor", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "factor_x", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "percentx");
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Relative Size X", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "factor_y", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "percenty");
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Relative Size Y", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "filter_type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "filtertype");
	RNA_def_property_enum_items(prop, filter_type_items);
	RNA_def_property_ui_text(prop, "Filter Type", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "use_bokeh", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "bokeh", 1);
	RNA_def_property_ui_text(prop, "Bokeh", "Uses circular filter (slower)");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "use_gamma_correction", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "gamma", 1);
	RNA_def_property_ui_text(prop, "Gamma", "Applies filter on gamma corrected values");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
}

static void def_cmp_filter(StructRNA *srna)
{
	PropertyRNA *prop;

	prop = RNA_def_property(srna, "filter_type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "custom1");
	RNA_def_property_enum_items(prop, node_filter_items);
	RNA_def_property_ui_text(prop, "Filter Type", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_cmp_map_value(StructRNA *srna)
{
	PropertyRNA *prop;
	
	RNA_def_struct_sdna_from(srna, "TexMapping", "storage");
	
	prop = RNA_def_property(srna, "offset", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "loc");
	RNA_def_property_array(prop, 1);
	RNA_def_property_range(prop, -1000.0f, 1000.0f);
	RNA_def_property_ui_text(prop, "Offset", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "size", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "size");
	RNA_def_property_array(prop, 1);
	RNA_def_property_range(prop, -1000.0f, 1000.0f);
	RNA_def_property_ui_text(prop, "Size", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "use_min", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", TEXMAP_CLIP_MIN);
	RNA_def_property_ui_text(prop, "Use Minimum", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "use_max", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", TEXMAP_CLIP_MAX);
	RNA_def_property_ui_text(prop, "Use Maximum", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "min", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "min");
	RNA_def_property_array(prop, 1);
	RNA_def_property_range(prop, -1000.0f, 1000.0f);
	RNA_def_property_ui_text(prop, "Minimum", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "max", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "max");
	RNA_def_property_array(prop, 1);
	RNA_def_property_range(prop, -1000.0f, 1000.0f);
	RNA_def_property_ui_text(prop, "Maximum", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_cmp_vector_blur(StructRNA *srna)
{
	PropertyRNA *prop;
	
	RNA_def_struct_sdna_from(srna, "NodeBlurData", "storage");
	
	prop = RNA_def_property(srna, "samples", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "samples");
	RNA_def_property_range(prop, 1, 256);
	RNA_def_property_ui_text(prop, "Samples", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "speed_min", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "minspeed");
	RNA_def_property_range(prop, 0, 1024);
	RNA_def_property_ui_text(prop, "Min Speed", "Minimum speed for a pixel to be blurred; used to separate background from foreground");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
		
	prop = RNA_def_property(srna, "speed_max", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "maxspeed");
	RNA_def_property_range(prop, 0, 1024);
	RNA_def_property_ui_text(prop, "Max Speed", "Maximum speed, or zero for none");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "factor", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "fac");
	RNA_def_property_range(prop, 0.0f, 2.0f);
	RNA_def_property_ui_text(prop, "Blur Factor", "Scaling factor for motion vectors; actually 'shutter speed' in frames");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "use_curved", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "curved", 1);
	RNA_def_property_ui_text(prop, "Curved", "Interpolate between frames in a bezier curve, rather than linearly");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_cmp_levels(StructRNA *srna)
{
	PropertyRNA *prop;
	
	static EnumPropertyItem channel_items[] = {
		{1, "COMBINED_RGB", 0, "C", "Combined RGB"},
		{2, "RED", 0, "R", "Red Channel"},
		{3, "GREEN", 0, "G", "Green Channel"},
		{4, "BLUE", 0, "B", "Blue Channel"},
		{5, "LUMINANCE", 0, "L", "Luminance Channel"},
		{0, NULL, 0, NULL, NULL}};
	
	prop = RNA_def_property(srna, "channel", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "custom1");
	RNA_def_property_enum_items(prop, channel_items);
	RNA_def_property_ui_text(prop, "Channel", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_cmp_image(StructRNA *srna)
{
	PropertyRNA *prop;
	
	/*
	 static EnumPropertyItem type_items[] = {
		{IMA_SRC_FILE,      "IMAGE",     0, "Image",     ""},
		{IMA_SRC_MOVIE,     "MOVIE",     "Movie",     ""},
		{IMA_SRC_SEQUENCE,  "SEQUENCE",  "Sequence",  ""},
		{IMA_SRC_GENERATED, "GENERATED", "Generated", ""},
		{0, NULL, 0, NULL, NULL}};
	*/
	
	prop = RNA_def_property(srna, "image", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "id");
	RNA_def_property_struct_type(prop, "Image");
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Image", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	RNA_def_struct_sdna_from(srna, "ImageUser", "storage");
	
	prop = RNA_def_property(srna, "frame_duration", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "frames");
	RNA_def_property_range(prop, 0, MAXFRAMEF);
	RNA_def_property_ui_text(prop, "Frames", "Number of images used in animation");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "frame_start", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "sfra");
	RNA_def_property_range(prop, MINAFRAMEF, MAXFRAMEF);
	RNA_def_property_ui_text(prop, "Start Frame", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "frame_offset", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "offset");
	RNA_def_property_range(prop, MINAFRAMEF, MAXFRAMEF);
	RNA_def_property_ui_text(prop, "Offset", "Offsets the number of the frame to use in the animation");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "use_cyclic", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "cycl", 1);
	RNA_def_property_ui_text(prop, "Cyclic", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "use_auto_refresh", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", IMA_ANIM_ALWAYS);
	RNA_def_property_ui_text(prop, "Auto-Refresh", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop= RNA_def_property(srna, "layer", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "layer");
	RNA_def_property_enum_items(prop, prop_image_layer_items);
	RNA_def_property_enum_funcs(prop, NULL, NULL, "rna_Node_image_layer_itemf");
	RNA_def_property_ui_text(prop, "Layer", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_image_layer_update");
}

static void def_cmp_render_layers(StructRNA *srna)
{
	PropertyRNA *prop;
	
	prop = RNA_def_property(srna, "scene", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "id");
	RNA_def_property_pointer_funcs(prop, NULL, "rna_Node_scene_set", NULL, NULL);
	RNA_def_property_struct_type(prop, "Scene");
	RNA_def_property_flag(prop, PROP_EDITABLE|PROP_ID_SELF_CHECK);
	RNA_def_property_ui_text(prop, "Scene", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop= RNA_def_property(srna, "layer", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "custom1");
	RNA_def_property_enum_items(prop, prop_scene_layer_items);
	RNA_def_property_enum_funcs(prop, NULL, NULL, "rna_Node_scene_layer_itemf");
	RNA_def_property_ui_text(prop, "Layer", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_cmp_output_file(StructRNA *srna)
{
	PropertyRNA *prop;
	
	static EnumPropertyItem type_items[] = {
		{R_TARGA,   "TARGA",        0, "Targa",        ""},
		{R_RAWTGA,  "RAW_TARGA",    0, "Targa Raw",    ""},
		{R_PNG,     "PNG",          0, "PNG",          ""},
		{R_BMP,     "BMP",          0, "BMP",          ""},
		{R_JPEG90,  "JPEG",         0, "JPEG",         ""},
		{R_IRIS,    "IRIS",         0, "IRIS",         ""},
		{R_RADHDR,  "RADIANCE_HDR", 0, "Radiance HDR", ""},
		{R_CINEON,  "CINEON",       0, "Cineon",       ""},
		{R_DPX,     "DPX",          0, "DPX",          ""},
		{R_OPENEXR, "OPENEXR",      0, "OpenEXR",      ""},
		{0, NULL, 0, NULL, NULL}};
	
	static EnumPropertyItem openexr_codec_items[] = {
		{0, "NONE",  0, "None",           ""},
		{1, "PXR24", 0, "Pxr24 (lossy)",  ""},
		{2, "ZIP",   0, "ZIP (lossless)", ""},
		{3, "PIZ",   0, "PIZ (lossless)", ""},
		{4, "RLE",   0, "RLE (lossless)", ""},
		{0, NULL, 0, NULL, NULL}};
	
	RNA_def_struct_sdna_from(srna, "NodeImageFile", "storage");
	
	prop = RNA_def_property(srna, "filepath", PROP_STRING, PROP_DIRPATH);
	RNA_def_property_string_sdna(prop, NULL, "name");
	RNA_def_property_ui_text(prop, "File Path", "Output path for the image, same functionality as render output.");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "image_type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "imtype");
	RNA_def_property_enum_items(prop, type_items);
	RNA_def_property_ui_text(prop, "Image Type", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "use_exr_half", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "subimtype", R_OPENEXR_HALF);
	RNA_def_property_ui_text(prop, "Half", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "exr_codec", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "codec");
	RNA_def_property_enum_items(prop, openexr_codec_items);
	RNA_def_property_ui_text(prop, "Codec", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "quality", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "quality");
	RNA_def_property_range(prop, 1, 100);
	RNA_def_property_ui_text(prop, "Quality", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");

	prop = RNA_def_property(srna, "frame_start", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "sfra");
	RNA_def_property_int_funcs(prop, NULL, "rna_Image_start_frame_set", NULL);
	RNA_def_property_range(prop, MINFRAMEF, MAXFRAMEF);
	RNA_def_property_ui_text(prop, "Start Frame", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "frame_end", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "efra");
	RNA_def_property_int_funcs(prop, NULL, "rna_Image_end_frame_set", NULL);
	RNA_def_property_range(prop, MINFRAMEF, MAXFRAMEF);
	RNA_def_property_ui_text(prop, "End Frame", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_cmp_dilate_erode(StructRNA *srna)
{
	PropertyRNA *prop;
	
	prop = RNA_def_property(srna, "distance", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "custom2");
	RNA_def_property_range(prop, -100, 100);
	RNA_def_property_ui_text(prop, "Distance", "Distance to grow/shrink (number of iterations)");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_cmp_scale(StructRNA *srna)
{
	PropertyRNA *prop;
	
	static EnumPropertyItem space_items[] = {
		{0, "RELATIVE",   0, "Relative",   ""},
		{1, "ABSOLUTE",   0, "Absolute",   ""},
		{2, "SCENE_SIZE", 0, "Scene Size", ""},
		{3, "RENDER_SIZE", 0, "Render Size", ""},
		{0, NULL, 0, NULL, NULL}};
	
	prop = RNA_def_property(srna, "space", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "custom1");
	RNA_def_property_enum_items(prop, space_items);
	RNA_def_property_ui_text(prop, "Space", "Coordinate space to scale relative to");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_cmp_rotate(StructRNA *srna)
{
	PropertyRNA *prop;
	
	static EnumPropertyItem rotate_items[] = {
		{0, "NEAREST",   0, "Nearest",   ""},
		{1, "BILINEAR",   0, "Bilinear",   ""},
		{2, "BICUBIC", 0, "Bicubic", ""},
		{0, NULL, 0, NULL, NULL}};
	
	prop = RNA_def_property(srna, "filter_type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "custom1");
	RNA_def_property_enum_items(prop, rotate_items);
	RNA_def_property_ui_text(prop, "Filter", "Method to use to filter rotation");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_cmp_diff_matte(StructRNA *srna)
{
	PropertyRNA *prop;

	RNA_def_struct_sdna_from(srna, "NodeChroma", "storage");
	
	prop = RNA_def_property(srna, "tolerance", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "t1");
	RNA_def_property_float_funcs(prop, NULL, "rna_Matte_t1_set", NULL);
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Tolerance", "Color distances below this threshold are keyed");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "falloff", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "t2");
	RNA_def_property_float_funcs(prop, NULL, "rna_Matte_t2_set", NULL);
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Falloff", "Color distances below this additional threshold are partially keyed");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_cmp_color_matte(StructRNA *srna)
{
	PropertyRNA *prop;
	
	RNA_def_struct_sdna_from(srna, "NodeChroma", "storage");

	prop = RNA_def_property(srna, "color_hue", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "t1");
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "H", "Hue tolerance for colors to be considered a keying color");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "color_saturation", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "t2");
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "S", "Saturation Tolerance for the color");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "color_value", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "t3");
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "V", "Value Tolerance for the color");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_cmp_distance_matte(StructRNA *srna)
{
	PropertyRNA *prop;
	
	RNA_def_struct_sdna_from(srna, "NodeChroma", "storage");
	
	prop = RNA_def_property(srna, "tolerance", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "t1");
	RNA_def_property_float_funcs(prop, NULL, "rna_Matte_t1_set", NULL);
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Tolerance", "Color distances below this threshold are keyed");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "falloff", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "t2");
	RNA_def_property_float_funcs(prop, NULL, "rna_Matte_t2_set", NULL);
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Falloff", "Color distances below this additional threshold are partially keyed");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_cmp_color_spill(StructRNA *srna)
{
	PropertyRNA *prop;

	static EnumPropertyItem channel_items[] = {
		{1, "R", 0, "R", "Red Spill Suppression"},
		{2, "G", 0, "G", "Green Spill Suppression"},
		{3, "B", 0, "B", "Blue Spill Suppression"},
		{0, NULL, 0, NULL, NULL}};

	static EnumPropertyItem limit_channel_items[] = {
		{1, "R", 0, "R", "Limit by Red"},
		{2, "G", 0, "G", "Limit by Green"},
		{3, "B", 0, "B", "Limit by Blue"},
		{0, NULL, 0, NULL, NULL}};

	static EnumPropertyItem algorithm_items[] = {
		{0, "SIMPLE", 0, "Simple", "Simple Limit Algorithm"},
		{1, "AVERAGE", 0, "Average", "Average Limit Algorithm"},
		{0, NULL, 0, NULL, NULL}};
	
	prop = RNA_def_property(srna, "channel", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "custom1");
	RNA_def_property_enum_items(prop, channel_items);
	RNA_def_property_ui_text(prop, "Channel", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "limit_method", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "custom2");
	RNA_def_property_enum_items(prop, algorithm_items);
	RNA_def_property_ui_text(prop, "Algorithm", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");

	RNA_def_struct_sdna_from(srna, "NodeColorspill", "storage");

	prop = RNA_def_property(srna, "limit_channel", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "limchan");
	RNA_def_property_enum_items(prop, limit_channel_items);
	RNA_def_property_ui_text(prop, "Limit Channel", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");

	prop = RNA_def_property(srna, "ratio", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "limscale");
	RNA_def_property_range(prop, 0.5f, 1.5f);
	RNA_def_property_ui_text(prop, "Ratio", "Scale limit by value");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");

	prop = RNA_def_property(srna, "use_unspill", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "unspill", 0);
	RNA_def_property_ui_text(prop, "Unspill", "Compensate all channels (diffenrently) by hand");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");

	prop = RNA_def_property(srna, "unspill_red", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "uspillr");
	RNA_def_property_range(prop, 0.0f, 1.5f);
	RNA_def_property_ui_text(prop, "R", "Red spillmap scale");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");

	prop = RNA_def_property(srna, "unspill_green", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "uspillg");
	RNA_def_property_range(prop, 0.0f, 1.5f);
	RNA_def_property_ui_text(prop, "G", "Green spillmap scale");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");

	prop = RNA_def_property(srna, "unspill_blue", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "uspillb");
	RNA_def_property_range(prop, 0.0f, 1.5f);
	RNA_def_property_ui_text(prop, "B", "Blue spillmap scale");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_cmp_luma_matte(StructRNA *srna)
{
	PropertyRNA *prop;
	
	RNA_def_struct_sdna_from(srna, "NodeChroma", "storage");
	
	prop = RNA_def_property(srna, "limit_max", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "t1");
	RNA_def_property_float_funcs(prop, NULL, "rna_Matte_t1_set", NULL);
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "High", "Values higher than this setting are 100% opaque");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "limit_min", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "t2");
	RNA_def_property_float_funcs(prop, NULL, "rna_Matte_t2_set", NULL);
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Low", "Values lower than this setting are 100% keyed");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_cmp_chroma_matte(StructRNA *srna)
{
	PropertyRNA *prop;
	
	RNA_def_struct_sdna_from(srna, "NodeChroma", "storage");
	
	prop = RNA_def_property(srna, "tolerance", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "t1");
	RNA_def_property_float_funcs(prop, NULL, "rna_Matte_t1_set", NULL);
	RNA_def_property_range(prop, 1.0f, 80.0f);
	RNA_def_property_ui_text(prop, "Acceptance", "Tolerance for a color to be considered a keying color");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "threshold", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "t2");
	RNA_def_property_float_funcs(prop, NULL, "rna_Matte_t2_set", NULL);
	RNA_def_property_range(prop, 0.0f, 30.0f);
	RNA_def_property_ui_text(prop, "Cutoff", "Tolerance below which colors will be considered as exact matches");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");

	prop = RNA_def_property(srna, "lift", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "fsize");
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Lift", "Alpha lift");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "gain", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "fstrength");
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Gain", "Alpha gain");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "shadow_adjust", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "t3");
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Shadow Adjust", "Adjusts the brightness of any shadows captured");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_cmp_channel_matte(StructRNA *srna)
{
	PropertyRNA *prop;
	
	static EnumPropertyItem color_space_items[] = {
		{CMP_NODE_CHANNEL_MATTE_CS_RGB, "RGB", 0, "RGB",   "RGB Color Space"},
		{CMP_NODE_CHANNEL_MATTE_CS_HSV, "HSV", 0, "HSV",   "HSV Color Space"},
		{CMP_NODE_CHANNEL_MATTE_CS_YUV, "YUV", 0, "YUV",   "YUV Color Space"},
		{CMP_NODE_CHANNEL_MATTE_CS_YCC, "YCC", 0, "YCbCr", "YCbCr Color Space"},
		{0, NULL, 0, NULL, NULL}};

	static EnumPropertyItem algorithm_items[] = {
		{0, "SINGLE", 0, "Single", "Limit by single channel"},
		{1, "MAX", 0, "Max", "Limit by max of other channels "},
		{0, NULL, 0, NULL, NULL}};
	
	prop = RNA_def_property(srna, "color_space", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "custom1");
	RNA_def_property_enum_items(prop, color_space_items);
	RNA_def_property_ui_text(prop, "Color Space", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop= RNA_def_property(srna, "matte_channel", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "custom2");
	RNA_def_property_enum_items(prop, prop_tri_channel_items);
	RNA_def_property_enum_funcs(prop, NULL, NULL, "rna_Node_channel_itemf");
	RNA_def_property_ui_text(prop, "Channel", "Channel used to determine matte");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");

	RNA_def_struct_sdna_from(srna, "NodeChroma", "storage");

	prop = RNA_def_property(srna, "limit_method", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "algorithm");
	RNA_def_property_enum_items(prop, algorithm_items);
	RNA_def_property_ui_text(prop, "Algorithm", "Algorithm to use to limit channel");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");

	prop = RNA_def_property(srna, "limit_channel", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "channel");
   RNA_def_property_enum_items(prop, prop_tri_channel_items);
   RNA_def_property_enum_funcs(prop, NULL, NULL, "rna_Node_channel_itemf");
	RNA_def_property_ui_text(prop, "Limit Channel", "Limit by this channels value");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "limit_max", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "t1");
	RNA_def_property_float_funcs(prop, NULL, "rna_Matte_t1_set", NULL);
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "High", "Values higher than this setting are 100% opaque");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "limit_min", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "t2");
	RNA_def_property_float_funcs(prop, NULL, "rna_Matte_t2_set", NULL);
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Low", "Values lower than this setting are 100% keyed");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_cmp_flip(StructRNA *srna)
{
	PropertyRNA *prop;
	
	prop = RNA_def_property(srna, "axis", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "custom1");
	RNA_def_property_enum_items(prop, node_flip_items);
	RNA_def_property_ui_text(prop, "Axis", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_cmp_splitviewer(StructRNA *srna)
{
	PropertyRNA *prop;
	
	static EnumPropertyItem axis_items[] = {
		{0, "X",  0, "X",     ""},
		{1, "Y",  0, "Y",     ""},
		{0, NULL, 0, NULL, NULL}};
	
	prop = RNA_def_property(srna, "axis", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "custom2");
	RNA_def_property_enum_items(prop, axis_items);
	RNA_def_property_ui_text(prop, "Axis", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");

	prop = RNA_def_property(srna, "factor", PROP_INT, PROP_FACTOR);
	RNA_def_property_int_sdna(prop, NULL, "custom1");
	RNA_def_property_range(prop, 0, 100);
	RNA_def_property_ui_text(prop, "Factor", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_cmp_id_mask(StructRNA *srna)
{
	PropertyRNA *prop;
	
	prop = RNA_def_property(srna, "index", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "custom1");
	RNA_def_property_range(prop, 0, 10000);
	RNA_def_property_ui_text(prop, "Index", "Pass index number to convert to alpha");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_cmp_map_uv(StructRNA *srna)
{
	PropertyRNA *prop;

	prop = RNA_def_property(srna, "alpha", PROP_INT, PROP_FACTOR);
	RNA_def_property_int_sdna(prop, NULL, "custom1");
	RNA_def_property_range(prop, 0, 100);
	RNA_def_property_ui_text(prop, "Alpha", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_cmp_defocus(StructRNA *srna)
{
	PropertyRNA *prop;
	
	static EnumPropertyItem bokeh_items[] = {
		{8, "OCTAGON",  0, "Octagonal",  "8 sides"},
		{7, "HEPTAGON", 0, "Heptagonal", "7 sides"},
		{6, "HEXAGON",  0, "Hexagonal",  "6 sides"},
		{5, "PENTAGON", 0, "Pentagonal", "5 sides"},
		{4, "SQUARE",   0, "Square",     "4 sides"},
		{3, "TRIANGLE", 0, "Triangular", "3 sides"},
		{0, "CIRCLE",   0, "Circular",   ""},
		{0, NULL, 0, NULL, NULL}};
	
	RNA_def_struct_sdna_from(srna, "NodeDefocus", "storage");
	
	prop = RNA_def_property(srna, "bokeh", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "bktype");
	RNA_def_property_enum_items(prop, bokeh_items);
	RNA_def_property_ui_text(prop, "Bokeh Type", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");

	/* TODO: angle in degrees */		
	prop = RNA_def_property(srna, "angle", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "rotation");
	RNA_def_property_range(prop, 0, 90);
	RNA_def_property_ui_text(prop, "Angle", "Bokeh shape rotation offset in degrees");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "use_gamma_correction", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "gamco", 1);
	RNA_def_property_ui_text(prop, "Gamma Correction", "Enable gamma correction before and after main process");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");

	/* TODO */
	prop = RNA_def_property(srna, "f_stop", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "fstop");
	RNA_def_property_range(prop, 0.0f, 128.0f);
	RNA_def_property_ui_text(prop, "fStop", "Amount of focal blur, 128=infinity=perfect focus, half the value doubles the blur radius");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "blur_max", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "maxblur");
	RNA_def_property_range(prop, 0.0f, 10000.0f);
	RNA_def_property_ui_text(prop, "Max Blur", "blur limit, maximum CoC radius, 0=no limit");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "threshold", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "bthresh");
	RNA_def_property_range(prop, 0.0f, 100.0f);
	RNA_def_property_ui_text(prop, "Threshold", "CoC radius threshold, prevents background bleed on in-focus midground, 0=off");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "use_preview", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "preview", 1);
	RNA_def_property_ui_text(prop, "Preview", "Enable sampling mode, useful for preview when using low samplecounts");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "samples", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "samples");
	RNA_def_property_range(prop, 16, 256);
	RNA_def_property_ui_text(prop, "Samples", "Number of samples (16=grainy, higher=less noise)");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "use_zbuffer", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_negative_sdna(prop, NULL, "no_zbuf", 1);
	RNA_def_property_ui_text(prop, "Use Z-Buffer", "Disable when using an image as input instead of actual zbuffer (auto enabled if node not image based, eg. time node)");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "z_scale", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "scale");
	RNA_def_property_range(prop, 0.0f, 1000.0f);
	RNA_def_property_ui_text(prop, "Z-Scale", "Scales the Z input when not using a zbuffer, controls maximum blur designated by the color white or input value 1");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_cmp_invert(StructRNA *srna)
{
	PropertyRNA *prop;
	
	prop = RNA_def_property(srna, "invert_rgb", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "custom1", CMP_CHAN_RGB);
	RNA_def_property_ui_text(prop, "RGB", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "invert_alpha", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "custom1", CMP_CHAN_A);
	RNA_def_property_ui_text(prop, "Alpha", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_cmp_crop(StructRNA *srna)
{
	PropertyRNA *prop;
	
	prop = RNA_def_property(srna, "use_crop_size", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "custom1", 1);
	RNA_def_property_ui_text(prop, "Crop Image Size", "Whether to crop the size of the input image");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");

   prop = RNA_def_property(srna, "relative", PROP_BOOLEAN, PROP_NONE);
   RNA_def_property_boolean_sdna(prop, NULL, "custom2", 1);
   RNA_def_property_ui_text(prop, "Relative", "Use relative values to crop image");
   RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");

	RNA_def_struct_sdna_from(srna, "NodeTwoXYs", "storage");

	prop = RNA_def_property(srna, "min_x", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "x1");
	RNA_def_property_range(prop, 0, 10000);
	RNA_def_property_ui_text(prop, "X1", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "max_x", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "x2");
	RNA_def_property_range(prop, 0, 10000);
	RNA_def_property_ui_text(prop, "X2", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "min_y", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "y1");
	RNA_def_property_range(prop, 0, 10000);
	RNA_def_property_ui_text(prop, "Y1", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "max_y", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "y2");
	RNA_def_property_range(prop, 0, 10000);
	RNA_def_property_ui_text(prop, "Y2", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");

   prop = RNA_def_property(srna, "rel_min_x", PROP_FLOAT, PROP_NONE);
   RNA_def_property_float_sdna(prop, NULL, "fac_x1");
   RNA_def_property_range(prop, 0.0, 1.0);
   RNA_def_property_ui_text(prop, "X1", "");
   RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");

   prop = RNA_def_property(srna, "rel_max_x", PROP_FLOAT, PROP_NONE);
   RNA_def_property_float_sdna(prop, NULL, "fac_x2");
   RNA_def_property_range(prop, 0.0, 1.0);
   RNA_def_property_ui_text(prop, "X2", "");
   RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");

   prop = RNA_def_property(srna, "rel_min_y", PROP_FLOAT, PROP_NONE);
   RNA_def_property_float_sdna(prop, NULL, "fac_y1");
   RNA_def_property_range(prop, 0.0, 1.0);
   RNA_def_property_ui_text(prop, "Y1", "");
   RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");

   prop = RNA_def_property(srna, "rel_max_y", PROP_FLOAT, PROP_NONE);
   RNA_def_property_float_sdna(prop, NULL, "fac_y2");
   RNA_def_property_range(prop, 0.0, 1.0);
   RNA_def_property_ui_text(prop, "Y2", "");
   RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_cmp_dblur(StructRNA *srna)
{
	PropertyRNA *prop;
	
	RNA_def_struct_sdna_from(srna, "NodeDBlurData", "storage");
	
	prop = RNA_def_property(srna, "iterations", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "iter");
	RNA_def_property_range(prop, 1, 32);
	RNA_def_property_ui_text(prop, "Iterations", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "use_wrap", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "wrap", 1);
	RNA_def_property_ui_text(prop, "Wrap", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "center_x", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "center_x");
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Center X", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "center_y", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "center_y");
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Center Y", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "distance", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "distance");
	RNA_def_property_range(prop, -1.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Distance", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "angle", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "angle");
	RNA_def_property_range(prop, 0.0f, 360.0f);
	RNA_def_property_ui_text(prop, "Angle", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "spin", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "spin");
	RNA_def_property_range(prop, -360.0f, 360.0f);
	RNA_def_property_ui_text(prop, "Spin", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "zoom", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "zoom");
	RNA_def_property_range(prop, 0.0f, 100.0f);
	RNA_def_property_ui_text(prop, "Zoom", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_cmp_bilateral_blur(StructRNA *srna)
{
	PropertyRNA *prop;
	
	RNA_def_struct_sdna_from(srna, "NodeBilateralBlurData", "storage");
	
	prop = RNA_def_property(srna, "iterations", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "iter");
	RNA_def_property_range(prop, 1, 128);
	RNA_def_property_ui_text(prop, "Iterations", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "sigma_color", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "sigma_color");
	RNA_def_property_range(prop, 0.01f, 3.0f);
	RNA_def_property_ui_text(prop, "Color Sigma", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "sigma_space", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "sigma_space");
	RNA_def_property_range(prop, 0.01f, 30.0f);
	RNA_def_property_ui_text(prop, "Space Sigma", "");	
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_cmp_premul_key(StructRNA *srna)
{
	PropertyRNA *prop;
	
	static EnumPropertyItem type_items[] = {
		{0, "KEY_TO_PREMUL", 0, "Key to Premul", ""},
		{1, "PREMUL_TO_KEY", 0, "Premul to Key", ""},
		{0, NULL, 0, NULL, NULL}};
	
	prop = RNA_def_property(srna, "mapping", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "custom1");
	RNA_def_property_enum_items(prop, type_items);
	RNA_def_property_ui_text(prop, "Mapping", "Conversion between premultiplied alpha and key alpha");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
}

static void def_cmp_glare(StructRNA *srna)
{
	PropertyRNA *prop;
	
	static EnumPropertyItem type_items[] = {
		{3, "GHOSTS",      0, "Ghosts",      ""},
		{2, "STREAKS",     0, "Streaks",     ""},
		{1, "FOG_GLOW",    0, "Fog Glow",    ""},
		{0, "SIMPLE_STAR", 0, "Simple Star", ""},
		{0, NULL, 0, NULL, NULL}};
	
	static EnumPropertyItem quality_items[] = {
		{0, "HIGH",   0, "High",   ""},
		{1, "MEDIUM", 0, "Medium", ""},
		{2, "LOW",    0, "Low",    ""},
		{0, NULL, 0, NULL, NULL}};
	
	RNA_def_struct_sdna_from(srna, "NodeGlare", "storage");
	
	prop = RNA_def_property(srna, "glare_type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "type");
	RNA_def_property_enum_items(prop, type_items);
	RNA_def_property_ui_text(prop, "Glare Type", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "quality", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "quality");
	RNA_def_property_enum_items(prop, quality_items);
	RNA_def_property_ui_text(prop, "Quality", "If not set to high quality, the effect will be applied to a low-res copy of the source image");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "iterations", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "iter");
	RNA_def_property_range(prop, 2, 5);
	RNA_def_property_ui_text(prop, "Iterations", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "color_modulation", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "colmod");
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Color Modulation", "Amount of Color Modulation, modulates colors of streaks and ghosts for a spectral dispersion effect");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "mix", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "mix");
	RNA_def_property_range(prop, -1.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Mix", "-1 is original image only, 0 is exact 50/50 mix, 1 is processed image only");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "threshold", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "threshold");
	RNA_def_property_range(prop, 0.0f, 1000.0f);
	RNA_def_property_ui_text(prop, "Threshold", "The glare filter will only be applied to pixels brighter than this value");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "streaks", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "angle");
	RNA_def_property_range(prop, 2, 16);
	RNA_def_property_ui_text(prop, "Streaks", "Total number of streaks");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "angle_offset", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "angle_ofs");
	RNA_def_property_range(prop, 0.0f, 180.0f);
	RNA_def_property_ui_text(prop, "Angle Offset", "Streak angle offset in degrees");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "fade", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "fade");
	RNA_def_property_range(prop, 0.75f, 1.0f);
	RNA_def_property_ui_text(prop, "Fade", "Streak fade-out factor");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "use_rotate_45", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "angle", 0);
	RNA_def_property_ui_text(prop, "Rotate 45", "Simple star filter: add 45 degree rotation offset");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "size", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "size");
	RNA_def_property_range(prop, 6, 9);
	RNA_def_property_ui_text(prop, "Size", "Glow/glare size (not actual size; relative to initial size of bright area of pixels)");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	/* TODO */
}

static void def_cmp_tonemap(StructRNA *srna)
{
	PropertyRNA *prop;
	
	static EnumPropertyItem type_items[] = {
		{1, "RD_PHOTORECEPTOR", 0, "R/D Photoreceptor", ""},
		{0, "RH_SIMPLE",        0, "Rh Simple",         ""},
		{0, NULL, 0, NULL, NULL}};
	
	RNA_def_struct_sdna_from(srna, "NodeTonemap", "storage");
	
	prop = RNA_def_property(srna, "tonemap_type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "type");
	RNA_def_property_enum_items(prop, type_items);
	RNA_def_property_ui_text(prop, "Tonemap Type", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "key", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "key");
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Key", "The value the average luminance is mapped to");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "offset", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "offset");
	RNA_def_property_range(prop, 0.001f, 10.0f);
	RNA_def_property_ui_text(prop, "Offset", "Normally always 1, but can be used as an extra control to alter the brightness curve");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "gamma", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "gamma");
	RNA_def_property_range(prop, 0.001f, 3.0f);
	RNA_def_property_ui_text(prop, "Gamma", "If not used, set to 1");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "intensity", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "f");
	RNA_def_property_range(prop, -8.0f, 8.0f);
	RNA_def_property_ui_text(prop, "Intensity", "If less than zero, darkens image; otherwise, makes it brighter");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "contrast", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "m");
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Contrast", "Set to 0 to use estimate from input image");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "adaptation", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "a");
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Adaptation", "If 0, global; if 1, based on pixel intensity");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "correction", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "c");
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Color Correction", "If 0, same for all channels; if 1, each independent");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_cmp_lensdist(StructRNA *srna)
{
	PropertyRNA *prop;
	
	RNA_def_struct_sdna_from(srna, "NodeLensDist", "storage");
	
	prop = RNA_def_property(srna, "use_projector", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "proj", 1);
	RNA_def_property_ui_text(prop, "Projector", "Enable/disable projector mode. Effect is applied in horizontal direction only");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "use_jitter", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "jit", 1);
	RNA_def_property_ui_text(prop, "Jitter", "Enable/disable jittering; faster, but also noisier");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "use_fit", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "fit", 1);
	RNA_def_property_ui_text(prop, "Fit", "For positive distortion factor only: scale image such that black areas are not visible");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_cmp_colorbalance(StructRNA *srna)
{
	PropertyRNA *prop;
	static float default_1[3] = {1.f, 1.f, 1.f};
	
	static EnumPropertyItem type_items[] = {
		{0, "LIFT_GAMMA_GAIN",      0, "Lift/Gamma/Gain",      ""},
		{1, "OFFSET_POWER_SLOPE",     0, "Offset/Power/Slope (ASC-CDL)",     "ASC-CDL standard color correction"},
		{0, NULL, 0, NULL, NULL}};
	
	prop = RNA_def_property(srna, "correction_method", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "custom1");
	RNA_def_property_enum_items(prop, type_items);
	RNA_def_property_ui_text(prop, "Correction Formula", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	RNA_def_struct_sdna_from(srna, "NodeColorBalance", "storage");
	
	prop = RNA_def_property(srna, "lift", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "lift");
	RNA_def_property_array(prop, 3);
	RNA_def_property_float_array_default(prop, default_1);
	RNA_def_property_ui_range(prop, 0, 2, 0.1, 3);
	RNA_def_property_ui_text(prop, "Lift", "Correction for Shadows");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "gamma", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "gamma");
	RNA_def_property_array(prop, 3);
	RNA_def_property_float_array_default(prop, default_1);
	RNA_def_property_ui_range(prop, 0, 2, 0.1, 3);
	RNA_def_property_ui_text(prop, "Gamma", "Correction for Midtones");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "gain", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "gain");
	RNA_def_property_array(prop, 3);
	RNA_def_property_float_array_default(prop, default_1);
	RNA_def_property_ui_range(prop, 0, 2, 0.1, 3);
	RNA_def_property_ui_text(prop, "Gain", "Correction for Highlights");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	
	prop = RNA_def_property(srna, "offset", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "lift");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_range(prop, 0, 1, 0.1, 3);
	RNA_def_property_ui_text(prop, "Offset", "Correction for Shadows");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "power", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "gamma");
	RNA_def_property_array(prop, 3);
	RNA_def_property_float_array_default(prop, default_1);
	RNA_def_property_ui_range(prop, 0, 2, 0.1, 3);
	RNA_def_property_ui_text(prop, "Power", "Correction for Midtones");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "slope", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "gain");
	RNA_def_property_array(prop, 3);
	RNA_def_property_float_array_default(prop, default_1);
	RNA_def_property_ui_range(prop, 0, 2, 0.1, 3);
	RNA_def_property_ui_text(prop, "Slope", "Correction for Highlights");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_cmp_huecorrect(StructRNA *srna)
{
	PropertyRNA *prop;
	
	prop = RNA_def_property(srna, "mapping", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "storage");
	RNA_def_property_struct_type(prop, "CurveMapping");
	RNA_def_property_ui_text(prop, "Mapping", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_cmp_zcombine(StructRNA *srna)
{
	PropertyRNA *prop;
	
	prop = RNA_def_property(srna, "use_alpha", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "custom1", 0);
	RNA_def_property_ui_text(prop, "Use Alpha", "Takes Alpha channel into account when doing the Z operation");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}


/* -- Texture Nodes --------------------------------------------------------- */

static void def_tex_output(StructRNA *srna)
{
	PropertyRNA *prop;

	RNA_def_struct_sdna_from(srna, "TexNodeOutput", "storage");
	
	prop = RNA_def_property(srna, "filepath", PROP_STRING, PROP_NONE);
	RNA_def_property_string_sdna(prop, NULL, "name");
	RNA_def_property_ui_text(prop, "Output Name", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

static void def_tex_image(StructRNA *srna)
{
	PropertyRNA *prop;

	prop = RNA_def_property(srna, "image", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "id");
	RNA_def_property_struct_type(prop, "Image");
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Image", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	/* is this supposed to be exposed? not sure..
	prop = RNA_def_property(srna, "settings", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "storage");
	RNA_def_property_struct_type(prop, "ImageUser");
	RNA_def_property_ui_text(prop, "Settings", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	 */
}

static void def_tex_bricks(StructRNA *srna)
{
	PropertyRNA *prop;

	prop = RNA_def_property(srna, "offset", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "custom3");
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Offset Amount", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "offset_frequency", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "custom1");
	RNA_def_property_range(prop, 2, 99);
	RNA_def_property_ui_text(prop, "Offset Frequency", "Offset every N rows");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "squash", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "custom4");
	RNA_def_property_range(prop, 0.0f, 99.0f);
	RNA_def_property_ui_text(prop, "Squash Amount", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "squash_frequency", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "custom2");
	RNA_def_property_range(prop, 2, 99);
	RNA_def_property_ui_text(prop, "Squash Frequency", "Squash every N rows");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
}

/* -------------------------------------------------------------------------- */

static EnumPropertyItem shader_node_type_items[MaxNodes];
static void rna_def_shader_node(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;
	
	alloc_node_type_items(shader_node_type_items, Category_ShaderNode);

	srna = RNA_def_struct(brna, "ShaderNode", "Node");
	RNA_def_struct_ui_text(srna, "Shader Node", "Material shader node");
	RNA_def_struct_sdna(srna, "bNode");

	prop = RNA_def_property(srna, "type", PROP_ENUM, PROP_NONE);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_enum_items(prop, shader_node_type_items);
	RNA_def_property_ui_text(prop, "Type", "");
}

static EnumPropertyItem compositor_node_type_items[MaxNodes];
static void rna_def_compositor_node(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;
	
	alloc_node_type_items(compositor_node_type_items, Category_CompositorNode);
	
	srna = RNA_def_struct(brna, "CompositorNode", "Node");
	RNA_def_struct_ui_text(srna, "Compositor Node", "");
	RNA_def_struct_sdna(srna, "bNode");

	prop = RNA_def_property(srna, "type", PROP_ENUM, PROP_NONE);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_enum_items(prop, compositor_node_type_items);
	RNA_def_property_ui_text(prop, "Type", "");
}

static EnumPropertyItem texture_node_type_items[MaxNodes];
static void rna_def_texture_node(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;
	
	alloc_node_type_items(texture_node_type_items, Category_TextureNode);
	
	srna = RNA_def_struct(brna, "TextureNode", "Node");
	RNA_def_struct_ui_text(srna, "Texture Node", "");
	RNA_def_struct_sdna(srna, "bNode");

	prop = RNA_def_property(srna, "type", PROP_ENUM, PROP_NONE);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_enum_items(prop, texture_node_type_items);
	RNA_def_property_ui_text(prop, "Type", "");
}

/* -------------------------------------------------------------------------- */

static void rna_def_nodetree_link_api(BlenderRNA *brna, PropertyRNA *cprop)
{
	StructRNA *srna;
	PropertyRNA *parm;
	FunctionRNA *func;

	RNA_def_property_srna(cprop, "NodeLinks");
	srna= RNA_def_struct(brna, "NodeLinks", NULL);
	RNA_def_struct_sdna(srna, "bNodeTree");
	RNA_def_struct_ui_text(srna, "Node Links", "Collection of Node Links");

	func= RNA_def_function(srna, "new", "rna_NodeTree_link_new");
	RNA_def_function_ui_description(func, "Add a node link to this node tree.");
	RNA_def_function_flag(func, FUNC_USE_REPORTS);
	parm= RNA_def_pointer(func, "input", "NodeSocket", "", "The input socket.");
	RNA_def_property_flag(parm, PROP_REQUIRED);
	parm= RNA_def_pointer(func, "output", "NodeSocket", "", "The output socket.");
	RNA_def_property_flag(parm, PROP_REQUIRED);
	/* return */
	parm= RNA_def_pointer(func, "link", "NodeLink", "", "New node link.");
	RNA_def_function_return(func, parm);

	func= RNA_def_function(srna, "remove", "rna_NodeTree_link_remove");
	RNA_def_function_ui_description(func, "remove a node link from the node tree.");
	RNA_def_function_flag(func, FUNC_USE_REPORTS);
	parm= RNA_def_pointer(func, "link", "NodeLink", "", "The node link to remove.");
	RNA_def_property_flag(parm, PROP_REQUIRED);
}

static void rna_def_composite_nodetree_api(BlenderRNA *brna, PropertyRNA *cprop)
{
	StructRNA *srna;
	PropertyRNA *parm;
	FunctionRNA *func;

	RNA_def_property_srna(cprop, "CompositorNodes");
	srna= RNA_def_struct(brna, "CompositorNodes", NULL);
	RNA_def_struct_sdna(srna, "bNodeTree");
	RNA_def_struct_ui_text(srna, "Compositor Nodes", "Collection of Compositor Nodes");

	func= RNA_def_function(srna, "new", "rna_NodeTree_node_composite_new");
	RNA_def_function_ui_description(func, "Add a node to this node tree.");
	RNA_def_function_flag(func, FUNC_USE_CONTEXT|FUNC_USE_REPORTS);
	parm= RNA_def_enum(func, "type", compositor_node_type_items, 0, "Type", "Type of node to add");
	RNA_def_property_flag(parm, PROP_REQUIRED);
	parm= RNA_def_pointer(func, "group", "NodeTree", "", "The group tree");
	/* return value */
	parm= RNA_def_pointer(func, "node", "Node", "", "New node.");
	RNA_def_function_return(func, parm);

	func= RNA_def_function(srna, "remove", "rna_NodeTree_node_remove");
	RNA_def_function_ui_description(func, "remove a node from this node tree.");
	RNA_def_function_flag(func, FUNC_USE_REPORTS);
	parm= RNA_def_pointer(func, "node", "Node", "", "The node to remove.");
	RNA_def_property_flag(parm, PROP_REQUIRED);
}

static void rna_def_shader_nodetree_api(BlenderRNA *brna, PropertyRNA *cprop)
{
	StructRNA *srna;
	PropertyRNA *parm;
	FunctionRNA *func;

	RNA_def_property_srna(cprop, "ShaderNodes");
	srna= RNA_def_struct(brna, "ShaderNodes", NULL);
	RNA_def_struct_sdna(srna, "bNodeTree");
	RNA_def_struct_ui_text(srna, "Shader Nodes", "Collection of Shader Nodes");

	func= RNA_def_function(srna, "new", "rna_NodeTree_node_new");
	RNA_def_function_ui_description(func, "Add a node to this node tree.");
	RNA_def_function_flag(func, FUNC_USE_CONTEXT|FUNC_USE_REPORTS);
	parm= RNA_def_enum(func, "type", shader_node_type_items, 0, "Type", "Type of node to add");
	RNA_def_property_flag(parm, PROP_REQUIRED);
	parm= RNA_def_pointer(func, "group", "NodeTree", "", "The group tree");
	/* return value */
	parm= RNA_def_pointer(func, "node", "Node", "", "New node.");
	RNA_def_function_return(func, parm);

	func= RNA_def_function(srna, "remove", "rna_NodeTree_node_remove");
	RNA_def_function_ui_description(func, "remove a node from this node tree.");
	RNA_def_function_flag(func, FUNC_USE_REPORTS);
	parm= RNA_def_pointer(func, "node", "Node", "", "The node to remove.");
	RNA_def_property_flag(parm, PROP_REQUIRED);
}

static void rna_def_texture_nodetree_api(BlenderRNA *brna, PropertyRNA *cprop)
{
	StructRNA *srna;
	PropertyRNA *parm;
	FunctionRNA *func;

	RNA_def_property_srna(cprop, "TextureNodes");
	srna= RNA_def_struct(brna, "TextureNodes", NULL);
	RNA_def_struct_sdna(srna, "bNodeTree");
	RNA_def_struct_ui_text(srna, "Texture Nodes", "Collection of Texture Nodes");

	func= RNA_def_function(srna, "new", "rna_NodeTree_node_texture_new");
	RNA_def_function_ui_description(func, "Add a node to this node tree.");
	RNA_def_function_flag(func, FUNC_USE_CONTEXT|FUNC_USE_REPORTS);
	parm= RNA_def_enum(func, "type", texture_node_type_items, 0, "Type", "Type of node to add");
	RNA_def_property_flag(parm, PROP_REQUIRED);
	parm= RNA_def_pointer(func, "group", "NodeTree", "", "The group tree");
	/* return value */
	parm= RNA_def_pointer(func, "node", "Node", "", "New node.");
	RNA_def_function_return(func, parm);

	func= RNA_def_function(srna, "remove", "rna_NodeTree_node_remove");
	RNA_def_function_ui_description(func, "remove a node from this node tree.");
	RNA_def_function_flag(func, FUNC_USE_REPORTS);
	parm= RNA_def_pointer(func, "node", "Node", "", "The node to remove.");
	RNA_def_property_flag(parm, PROP_REQUIRED);
}

static void rna_def_node_socket(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	static EnumPropertyItem node_socket_type_items[] = {
		{SOCK_VALUE,  "VALUE",     0,    "Value",     ""},
		{SOCK_VECTOR, "VECTOR",    0,    "Vector",    ""},
		{SOCK_RGBA,   "RGBA",      0,    "RGBA",      ""},
		{0, NULL, 0, NULL, NULL}};

	srna = RNA_def_struct(brna, "NodeSocket", NULL);
	RNA_def_struct_ui_text(srna, "Node Socket", "Input or output socket of a node");
	RNA_def_struct_refine_func(srna, "rna_NodeSocketType_refine");
	RNA_def_struct_sdna(srna, "bNodeSocket");
	RNA_def_struct_ui_icon(srna, ICON_PLUG);
	RNA_def_struct_path_func(srna, "rna_NodeSocket_path");

	prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Name", "Socket name");
	RNA_def_struct_name_property(srna, prop);

	/* can add back if there is any use in reading them */
#if 0
	prop = RNA_def_property(srna, "min", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "ns.min");
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Minimum Value", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_NodeSocket_update");

	prop = RNA_def_property(srna, "max", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "ns.max");
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Maximum Value", "");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_NodeSocket_update");
#endif

	prop = RNA_def_property(srna, "type", PROP_ENUM, PROP_NONE);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_enum_items(prop, node_socket_type_items);
	RNA_def_property_ui_text(prop, "Type", "Node Socket type");
}

static void rna_def_node_socket_value(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "ValueNodeSocket", "NodeSocket");
	RNA_def_struct_ui_text(srna, "Value Node Socket", "Input or output socket of a node");
	RNA_def_struct_sdna(srna, "bNodeSocket");
	RNA_def_struct_ui_icon(srna, ICON_PLUG);
	RNA_def_struct_path_func(srna, "rna_NodeSocket_path");

	prop = RNA_def_property(srna, "default_value", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "ns.vec");
	RNA_def_property_array(prop, 1);
	RNA_def_property_ui_text(prop, "Default Value", "Default value of the socket when no link is attached");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_NodeSocket_update");
	RNA_def_property_float_funcs(prop, NULL, NULL, "rna_NodeSocket_defvalue_range");
}

static void rna_def_node_socket_vector(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "VectorNodeSocket", "NodeSocket");
	RNA_def_struct_ui_text(srna, "Vector Node Socket", "Input or output socket of a node");
	RNA_def_struct_sdna(srna, "bNodeSocket");
	RNA_def_struct_ui_icon(srna, ICON_PLUG);
	RNA_def_struct_path_func(srna, "rna_NodeSocket_path");

	prop = RNA_def_property(srna, "default_value", PROP_FLOAT, PROP_XYZ);
	RNA_def_property_float_sdna(prop, NULL, "ns.vec");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Default Value", "Default value of the socket when no link is attached");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_NodeSocket_update");
	RNA_def_property_float_funcs(prop, NULL, NULL, "rna_NodeSocket_defvalue_range");
}

static void rna_def_node_socket_rgba(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "RGBANodeSocket", "NodeSocket");
	RNA_def_struct_ui_text(srna, "RGBA Node Socket", "Input or output socket of a node");
	RNA_def_struct_sdna(srna, "bNodeSocket");
	RNA_def_struct_ui_icon(srna, ICON_PLUG);
	RNA_def_struct_path_func(srna, "rna_NodeSocket_path");

	prop = RNA_def_property(srna, "default_value", PROP_FLOAT, PROP_COLOR);
	RNA_def_property_float_sdna(prop, NULL, "ns.vec");
	RNA_def_property_array(prop, 4);
	RNA_def_property_ui_text(prop, "Default Value", "Default value of the socket when no link is attached");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_NodeSocket_update");
	RNA_def_property_float_funcs(prop, NULL, NULL, "rna_NodeSocket_defvalue_range");
}

static void rna_def_node(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;
	
	srna = RNA_def_struct(brna, "Node", NULL);
	RNA_def_struct_ui_text(srna, "Node", "Node in a node tree");
	RNA_def_struct_sdna(srna, "bNode");
	RNA_def_struct_ui_icon(srna, ICON_NODE);
	RNA_def_struct_refine_func(srna, "rna_Node_refine");
	RNA_def_struct_path_func(srna, "rna_Node_path");
	
	prop = RNA_def_property(srna, "location", PROP_FLOAT, PROP_XYZ);
	RNA_def_property_float_sdna(prop, NULL, "locx");
	RNA_def_property_array(prop, 2);
	RNA_def_property_range(prop, -10000.0f, 10000.0f);
	RNA_def_property_ui_text(prop, "Location", "");
	RNA_def_property_update(prop, NC_NODE, "rna_Node_update");
	
	prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
	RNA_def_property_ui_text(prop, "Name", "Node name");
	RNA_def_struct_name_property(srna, prop);
	RNA_def_property_string_funcs(prop, NULL, NULL, "rna_Node_name_set");
	RNA_def_property_update(prop, NC_NODE|NA_EDITED, "rna_Node_update");
	
	prop = RNA_def_property(srna, "inputs", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_collection_sdna(prop, NULL, "inputs", NULL);
	RNA_def_property_struct_type(prop, "NodeSocket");
	RNA_def_property_ui_text(prop, "Inputs", "");
	
	prop = RNA_def_property(srna, "outputs", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_collection_sdna(prop, NULL, "outputs", NULL);
	RNA_def_property_struct_type(prop, "NodeSocket");
	RNA_def_property_ui_text(prop, "Outputs", "");
}

static void rna_def_node_link(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;
	
	srna = RNA_def_struct(brna, "NodeLink", NULL);
	RNA_def_struct_ui_text(srna, "NodeLink", "Link between nodes in a node tree");
	RNA_def_struct_sdna(srna, "bNodeLink");
	RNA_def_struct_ui_icon(srna, ICON_NODE);

	prop = RNA_def_property(srna, "from_node", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "fromnode");
	RNA_def_property_struct_type(prop, "Node");
	RNA_def_property_ui_text(prop, "From node", "");

	prop = RNA_def_property(srna, "to_node", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "tonode");
	RNA_def_property_struct_type(prop, "Node");
	RNA_def_property_ui_text(prop, "To node", "");

	prop = RNA_def_property(srna, "from_socket", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "fromsock");
	RNA_def_property_struct_type(prop, "NodeSocket");
	RNA_def_property_ui_text(prop, "From socket", "");

	prop = RNA_def_property(srna, "to_socket", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "tosock");
	RNA_def_property_struct_type(prop, "NodeSocket");
	RNA_def_property_ui_text(prop, "To socket", "");
}

static void rna_def_nodetree(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;
	/* FunctionRNA *func; */
	/* PropertyRNA *parm; */

	static EnumPropertyItem nodetree_type_items[] = {
		{NTREE_SHADER,      "SHADER",       0,    "Shader",       ""},
		{NTREE_COMPOSIT,    "COMPOSITE",    0,    "Composite",    ""},
		{NTREE_TEXTURE,     "TEXTURE",      0,    "Texture",      ""},
		{0, NULL, 0, NULL, NULL}};
	
	srna = RNA_def_struct(brna, "NodeTree", "ID");
	RNA_def_struct_ui_text(srna, "Node Tree", "Node tree consisting of linked nodes used for materials, textures and compositing");
	RNA_def_struct_sdna(srna, "bNodeTree");
	RNA_def_struct_ui_icon(srna, ICON_NODETREE);
	RNA_def_struct_refine_func(srna, "rna_NodeTree_refine");

	/* AnimData */
	rna_def_animdata_common(srna);
	
	/* NodeLinks Collection */
	prop = RNA_def_property(srna, "links", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_collection_sdna(prop, NULL, "links", NULL);
	RNA_def_property_struct_type(prop, "NodeLink");
	RNA_def_property_ui_text(prop, "Links", "");
	rna_def_nodetree_link_api(brna, prop);

	/* Grease Pencil */
	prop= RNA_def_property(srna, "grease_pencil", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "gpd");
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_struct_type(prop, "GreasePencil");
	RNA_def_property_ui_text(prop, "Grease Pencil Data", "Grease Pencil datablock");

	/* these are too much like operators, better to have data level access 
	 * ngroup = bpy.data.node_groups.new() 
	 * ngroup.nodes.new(....) etc. */
#if 0
	func= RNA_def_function(srna, "group_add", "nodeMakeGroupFromSelected");
	RNA_def_function_ui_description(func, "Make a group from the active nodes.");
	/* return */
	parm= RNA_def_pointer(func, "group", "Node", "", "New group.");
	RNA_def_function_return(func, parm);

	func= RNA_def_function(srna, "ungroup", "nodeGroupUnGroup");
	RNA_def_function_ui_description(func, "Ungroup node group");
	parm= RNA_def_pointer(func, "group", "Node", "", "The group to ungroup.");
	RNA_def_property_flag(parm, PROP_REQUIRED);
	parm= RNA_def_int(func, "bool", 0, 0, 1, "Bool", "", 0, 1);
	RNA_def_function_return(func, parm);
#endif
	
	prop = RNA_def_property(srna, "type", PROP_ENUM, PROP_NONE);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_enum_items(prop, nodetree_type_items);
	RNA_def_property_ui_text(prop, "Type", "Node Tree type");
}

static void rna_def_composite_nodetree(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "CompositorNodeTree", "NodeTree");
	RNA_def_struct_ui_text(srna, "Compositor Node Tree", "Node tree consisting of linked nodes used for compositing");
	RNA_def_struct_sdna(srna, "bNodeTree");
	RNA_def_struct_ui_icon(srna, ICON_NODETREE);

	/* Nodes Collection */
	prop = RNA_def_property(srna, "nodes", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_collection_sdna(prop, NULL, "nodes", NULL);
	RNA_def_property_struct_type(prop, "Node");
	RNA_def_property_ui_text(prop, "Nodes", "");

	rna_def_composite_nodetree_api(brna, prop);
}

static void rna_def_shader_nodetree(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "ShaderNodeTree", "NodeTree");
	RNA_def_struct_ui_text(srna, "Shader Node Tree", "Node tree consisting of linked nodes used for materials");
	RNA_def_struct_sdna(srna, "bNodeTree");
	RNA_def_struct_ui_icon(srna, ICON_NODETREE);

	/* Nodes Collection */
	prop = RNA_def_property(srna, "nodes", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_collection_sdna(prop, NULL, "nodes", NULL);
	RNA_def_property_struct_type(prop, "Node");
	RNA_def_property_ui_text(prop, "Nodes", "");

	rna_def_shader_nodetree_api(brna, prop);
}

static void rna_def_texture_nodetree(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "TextureNodeTree", "NodeTree");
	RNA_def_struct_ui_text(srna, "Texture Node Tree", "Node tree consisting of linked nodes used for textures");
	RNA_def_struct_sdna(srna, "bNodeTree");
	RNA_def_struct_ui_icon(srna, ICON_NODETREE);

	/* Nodes Collection */
	prop = RNA_def_property(srna, "nodes", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_collection_sdna(prop, NULL, "nodes", NULL);
	RNA_def_property_struct_type(prop, "Node");
	RNA_def_property_ui_text(prop, "Nodes", "");

	rna_def_texture_nodetree_api(brna, prop);
}

static void define_specific_node(BlenderRNA *brna, int id, void (*func)(StructRNA*))
{
	StructRNA *srna = def_node(brna, id);
	
	if(func)
		func(srna);
}

void RNA_def_nodetree(BlenderRNA *brna)
{
	init();
	rna_def_nodetree(brna);
	rna_def_node_socket(brna);
	rna_def_node_socket_value(brna);
	rna_def_node_socket_vector(brna);
	rna_def_node_socket_rgba(brna);
	rna_def_node(brna);
	rna_def_node_link(brna);
	rna_def_shader_node(brna);
	rna_def_compositor_node(brna);
	rna_def_texture_node(brna);
	rna_def_composite_nodetree(brna);
	rna_def_shader_nodetree(brna);
	rna_def_texture_nodetree(brna);
	#define DefNode(Category, ID, DefFunc, EnumName, StructName, UIName, UIDesc) \
		define_specific_node(brna, ID, DefFunc);
		
	#include "rna_nodetree_types.h"
	
	#undef DefNode
	
	define_specific_node(brna, NODE_GROUP, def_group);
}

#endif

