/**
 * PivotScaleTool — Unigine 2.6 Plugin (DLL)
 *
 * Features:
 *   - Scale Origin: Pivot Center / BBox Center / World Origin
 *   - Scale each object individually (one by one from its own origin)
 *   - Node list with multi-select: only selected nodes are processed
 *   - Refresh button to reload world nodes
 *
 * Uses the Unigine::Plugin class interface loaded via -extern_plugin.
 */

#include <UnigineEngine.h>
#include <UniginePlugin.h>
#include <UnigineGui.h>
#include <UnigineWidgets.h>
#include <UnigineWorld.h>
#include <UnigineNode.h>
#include <UnigineMathLib.h>
#include <UnigineLog.h>
#include <UnigineBounds.h>
#include <UnigineEditor.h>
#include <UnigineString.h>

#include <cstdlib>
#include <cstdio>

using namespace Unigine;
using namespace Unigine::Math;

// ---------------------------------------------------------------------------
// Scale origin modes
// ---------------------------------------------------------------------------
enum ScaleOrigin
{
	SCALE_FROM_PIVOT  = 0,
	SCALE_FROM_BBOX   = 1,
	SCALE_FROM_WORLD  = 2,
};

enum NodeFilter
{
	FILTER_ALL = 0,
	FILTER_MESH_DYNAMIC,
	FILTER_MESH_STATIC,
	FILTER_MESH_SKINNED,
	FILTER_MESH_CLUSTER,
	FILTER_MESH_CLUTTER,
	FILTER_ALL_OBJECTS,
	FILTER_LIGHTS,
	FILTER_DECALS,
	FILTER_PLAYERS,
};

static bool node_matches_filter(const NodePtr &n, int filter)
{
	if (!n) return false;
	int t = n->getType();
	switch (filter)
	{
	case FILTER_ALL:           return true;
	case FILTER_MESH_DYNAMIC:  return t == Node::OBJECT_MESH_DYNAMIC;
	case FILTER_MESH_STATIC:   return t == Node::OBJECT_MESH_STATIC;
	case FILTER_MESH_SKINNED:  return t == Node::OBJECT_MESH_SKINNED;
	case FILTER_MESH_CLUSTER:  return t == Node::OBJECT_MESH_CLUSTER;
	case FILTER_MESH_CLUTTER:  return t == Node::OBJECT_MESH_CLUTTER;
	case FILTER_ALL_OBJECTS:   return (t >= Node::OBJECT_BEGIN && t <= Node::OBJECT_END);
	case FILTER_LIGHTS:        return (t >= Node::LIGHT_BEGIN && t <= Node::LIGHT_END);
	case FILTER_DECALS:        return (t >= Node::DECAL_BEGIN && t <= Node::DECAL_END);
	case FILTER_PLAYERS:       return (t >= Node::PLAYER_BEGIN && t <= Node::PLAYER_END);
	default:                   return true;
	}
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool compute_bbox_center(const NodePtr &node, Vec3 &out_center)
{
	if (!node)
		return false;

	BoundBox bb = node->getBoundBox();
	if (!bb.isValid())
		return false;

	out_center = Vec3((bb.getMin() + bb.getMax()) * 0.5f);
	return true;
}

static void scale_node_from_pivot(const NodePtr &node, float scale_value)
{
	vec3 cur = node->getScale();
	node->setScale(cur * scale_value);
}

static void scale_node_from_bbox(const NodePtr &node, float scale_value)
{
	Vec3 bbox_center;
	if (!compute_bbox_center(node, bbox_center))
	{
		scale_node_from_pivot(node, scale_value);
		return;
	}

	Mat4 t = node->getTransform();
	Vec3 pivot_world = Vec3(t.getColumn3(3));
	Vec3 center_world = Vec3(t * Vec4(bbox_center, 1.0f));

	Vec3 new_pivot = center_world + (pivot_world - center_world) * static_cast<double>(scale_value);

	t.setColumn3(3, new_pivot);
	node->setTransform(t);

	vec3 cur = node->getScale();
	node->setScale(cur * scale_value);
}

static void scale_node_from_world_origin(const NodePtr &node, float scale_value)
{
	Mat4 t = node->getTransform();
	Vec3 pivot_world = Vec3(t.getColumn3(3));

	Vec3 new_pivot = pivot_world * static_cast<double>(scale_value);

	t.setColumn3(3, new_pivot);
	node->setTransform(t);

	vec3 cur = node->getScale();
	node->setScale(cur * scale_value);
}

// ---------------------------------------------------------------------------
// Plugin class
// ---------------------------------------------------------------------------

class PivotScaleToolPlugin;
static PivotScaleToolPlugin *g_instance = nullptr;

class PivotScaleToolPlugin : public Plugin
{
public:
	PivotScaleToolPlugin() {}
	virtual ~PivotScaleToolPlugin() {}

	virtual const char *get_name() override { return "PivotScaleTool"; }

	virtual int init() override
	{
		Log::message("PivotScaleTool: initializing...\n");

		GuiPtr gui = Gui::get();
		if (!gui)
		{
			Log::error("PivotScaleTool: failed to obtain Gui.\n");
			return 0;
		}

		// --- Window ---
		window = WidgetWindow::create(gui, "Pivot & Scale Tool", 4, 4);
		window->setPosition(50, 50);
		window->setSizeable(1);
		window->getWidget()->setWidth(280);
		window->getWidget()->setHeight(420);

		vbox = WidgetVBox::create(gui, 2, 2);
		window->addChild(vbox->getWidget(), Gui::ALIGN_EXPAND);

		// --- Scale Origin label ---
		lbl_origin = WidgetLabel::create(gui, "Scale From:");
		vbox->addChild(lbl_origin->getWidget(), Gui::ALIGN_EXPAND);

		// --- Scale Origin combo ---
		combo_origin = WidgetComboBox::create(gui);
		combo_origin->addItem("Pivot Center");
		combo_origin->addItem("BBox Center");
		combo_origin->addItem("World Origin");
		combo_origin->setCurrentItem(0);
		combo_origin->getWidget()->setWidth(260);
		vbox->addChild(combo_origin->getWidget(), Gui::ALIGN_EXPAND);

		// --- Scale Value label ---
		lbl_scale = WidgetLabel::create(gui, "Uniform Scale:");
		vbox->addChild(lbl_scale->getWidget(), Gui::ALIGN_EXPAND);

		// --- Scale Value edit ---
		edit_scale = WidgetEditLine::create(gui, "2.0");
		edit_scale->getWidget()->setWidth(260);
		vbox->addChild(edit_scale->getWidget(), Gui::ALIGN_EXPAND);

		// --- Offset label ---
		lbl_offset = WidgetLabel::create(gui, "Position Offset (X, Y, Z):");
		vbox->addChild(lbl_offset->getWidget(), Gui::ALIGN_EXPAND);

		// --- Offset X ---
		edit_offset_x = WidgetEditLine::create(gui, "0.0");
		edit_offset_x->getWidget()->setWidth(260);
		vbox->addChild(edit_offset_x->getWidget(), Gui::ALIGN_EXPAND);

		// --- Offset Y ---
		edit_offset_y = WidgetEditLine::create(gui, "0.0");
		edit_offset_y->getWidget()->setWidth(260);
		vbox->addChild(edit_offset_y->getWidget(), Gui::ALIGN_EXPAND);

		// --- Offset Z ---
		edit_offset_z = WidgetEditLine::create(gui, "0.0");
		edit_offset_z->getWidget()->setWidth(260);
		vbox->addChild(edit_offset_z->getWidget(), Gui::ALIGN_EXPAND);

		// --- Node Filter label ---
		lbl_filter = WidgetLabel::create(gui, "Show Node Type:");
		vbox->addChild(lbl_filter->getWidget(), Gui::ALIGN_EXPAND);

		// --- Node Filter combo ---
		combo_filter = WidgetComboBox::create(gui);
		combo_filter->addItem("All Nodes");
		combo_filter->addItem("ObjectMeshDynamic");
		combo_filter->addItem("ObjectMeshStatic");
		combo_filter->addItem("ObjectMeshSkinned");
		combo_filter->addItem("ObjectMeshCluster");
		combo_filter->addItem("ObjectMeshClutter");
		combo_filter->addItem("All Objects");
		combo_filter->addItem("Lights");
		combo_filter->addItem("Decals");
		combo_filter->addItem("Players");
		combo_filter->setCurrentItem(FILTER_MESH_DYNAMIC);
		combo_filter->setCallback0(Gui::CHANGED, MakeCallback(on_filter_changed_static));
		vbox->addChild(combo_filter->getWidget(), Gui::ALIGN_EXPAND);

		// --- Nodes label ---
		lbl_nodes = WidgetLabel::create(gui, "Nodes (Ctrl+Click = multi-select):");
		vbox->addChild(lbl_nodes->getWidget(), Gui::ALIGN_EXPAND);

		// --- Node ListBox inside ScrollBox ---
		scrollbox_nodes = WidgetScrollBox::create(gui, 2, 2);
		scrollbox_nodes->getWidget()->setHeight(200);
		scrollbox_nodes->setVScrollEnabled(1);
		scrollbox_nodes->setHScrollEnabled(0);
		scrollbox_nodes->setBorder(1);
		vbox->addChild(scrollbox_nodes->getWidget(), Gui::ALIGN_EXPAND);

		listbox_nodes = WidgetListBox::create(gui);
		listbox_nodes->setMultiSelection(1);
		scrollbox_nodes->addChild(listbox_nodes->getWidget(), Gui::ALIGN_EXPAND);

		// --- Refresh ---
		btn_refresh = WidgetButton::create(gui, "Refresh Nodes");
		btn_refresh->setCallback0(Gui::CLICKED, MakeCallback(on_refresh_static));
		vbox->addChild(btn_refresh->getWidget(), Gui::ALIGN_EXPAND);

		// --- Select All ---
		btn_select_all = WidgetButton::create(gui, "Select All");
		btn_select_all->setCallback0(Gui::CLICKED, MakeCallback(on_select_all_static));
		vbox->addChild(btn_select_all->getWidget(), Gui::ALIGN_EXPAND);

		// --- Select None ---
		btn_select_none = WidgetButton::create(gui, "Select None");
		btn_select_none->setCallback0(Gui::CLICKED, MakeCallback(on_select_none_static));
		vbox->addChild(btn_select_none->getWidget(), Gui::ALIGN_EXPAND);

		// --- Scale Up ---
		btn_apply = WidgetButton::create(gui, "Scale Up");
		btn_apply->setCallback0(Gui::CLICKED, MakeCallback(on_apply_static));
		vbox->addChild(btn_apply->getWidget(), Gui::ALIGN_EXPAND);

		// --- Scale Down ---
		btn_scale_down = WidgetButton::create(gui, "Scale Down");
		btn_scale_down->setCallback0(Gui::CLICKED, MakeCallback(on_scale_down_static));
		vbox->addChild(btn_scale_down->getWidget(), Gui::ALIGN_EXPAND);

		// --- Apply Offset ---
		btn_apply_offset = WidgetButton::create(gui, "Apply Offset");
		btn_apply_offset->setCallback0(Gui::CLICKED, MakeCallback(on_apply_offset_static));
		vbox->addChild(btn_apply_offset->getWidget(), Gui::ALIGN_EXPAND);

		// --- Label Gap section ---
		lbl_label_gap = WidgetLabel::create(gui, "Label Gap (_Label1 / _Label2):");
		vbox->addChild(lbl_label_gap->getWidget(), Gui::ALIGN_EXPAND);

		lbl_gap_axis = WidgetLabel::create(gui, "Gap Axis:");
		vbox->addChild(lbl_gap_axis->getWidget(), Gui::ALIGN_EXPAND);

		combo_gap_axis = WidgetComboBox::create(gui);
		combo_gap_axis->addItem("Z (Up/Down)");
		combo_gap_axis->addItem("Y (Forward/Back)");
		combo_gap_axis->addItem("X (Left/Right)");
		combo_gap_axis->setCurrentItem(0);
		vbox->addChild(combo_gap_axis->getWidget(), Gui::ALIGN_EXPAND);

		lbl_gap_value = WidgetLabel::create(gui, "Gap Distance:");
		vbox->addChild(lbl_gap_value->getWidget(), Gui::ALIGN_EXPAND);

		edit_gap_value = WidgetEditLine::create(gui, "1.0");
		vbox->addChild(edit_gap_value->getWidget(), Gui::ALIGN_EXPAND);

		btn_apply_gap = WidgetButton::create(gui, "Apply Label Gap");
		btn_apply_gap->setCallback0(Gui::CLICKED, MakeCallback(on_apply_label_gap_static));
		vbox->addChild(btn_apply_gap->getWidget(), Gui::ALIGN_EXPAND);

		// --- Status ---
		label_status = WidgetLabel::create(gui, "Click 'Refresh Nodes' to start.");
		vbox->addChild(label_status->getWidget(), Gui::ALIGN_EXPAND);

		gui->addChild(window->getWidget(), Gui::ALIGN_OVERLAP);

		// Auto-refresh on init
		on_refresh();

		Log::message("PivotScaleTool: initialized successfully.\n");
		return 1;
	}

	virtual int shutdown() override
	{
		Log::message("PivotScaleTool: shutting down...\n");

		GuiPtr gui = Gui::get();
		if (gui && window)
			gui->removeChild(window->getWidget());

		cached_nodes.clear();

		label_status     = WidgetLabelPtr();
		lbl_origin       = WidgetLabelPtr();
		lbl_scale        = WidgetLabelPtr();
		lbl_offset       = WidgetLabelPtr();
		lbl_filter       = WidgetLabelPtr();
		combo_filter     = WidgetComboBoxPtr();
		lbl_nodes        = WidgetLabelPtr();
		btn_apply        = WidgetButtonPtr();
		btn_scale_down   = WidgetButtonPtr();
		btn_apply_offset = WidgetButtonPtr();
		lbl_label_gap    = WidgetLabelPtr();
		lbl_gap_axis     = WidgetLabelPtr();
		combo_gap_axis   = WidgetComboBoxPtr();
		lbl_gap_value    = WidgetLabelPtr();
		edit_gap_value   = WidgetEditLinePtr();
		btn_apply_gap    = WidgetButtonPtr();
		btn_refresh      = WidgetButtonPtr();
		btn_select_all   = WidgetButtonPtr();
		btn_select_none  = WidgetButtonPtr();
		listbox_nodes    = WidgetListBoxPtr();
		scrollbox_nodes  = WidgetScrollBoxPtr();
		combo_origin     = WidgetComboBoxPtr();
		edit_scale       = WidgetEditLinePtr();
		edit_offset_x    = WidgetEditLinePtr();
		edit_offset_y    = WidgetEditLinePtr();
		edit_offset_z    = WidgetEditLinePtr();
		vbox             = WidgetVBoxPtr();
		window           = WidgetWindowPtr();

		Log::message("PivotScaleTool: shut down.\n");
		return 1;
	}

private:
	WidgetWindowPtr    window;
	WidgetVBoxPtr      vbox;
	WidgetLabelPtr     lbl_origin;
	WidgetComboBoxPtr  combo_origin;
	WidgetLabelPtr     lbl_scale;
	WidgetEditLinePtr  edit_scale;
	WidgetLabelPtr     lbl_offset;
	WidgetEditLinePtr  edit_offset_x;
	WidgetEditLinePtr  edit_offset_y;
	WidgetEditLinePtr  edit_offset_z;
	WidgetLabelPtr     lbl_filter;
	WidgetComboBoxPtr  combo_filter;
	WidgetLabelPtr     lbl_nodes;
	WidgetScrollBoxPtr scrollbox_nodes;
	WidgetListBoxPtr   listbox_nodes;
	WidgetButtonPtr    btn_refresh;
	WidgetButtonPtr    btn_select_all;
	WidgetButtonPtr    btn_select_none;
	WidgetButtonPtr    btn_apply;
	WidgetButtonPtr    btn_scale_down;
	WidgetButtonPtr    btn_apply_offset;
	WidgetLabelPtr     lbl_label_gap;
	WidgetLabelPtr     lbl_gap_axis;
	WidgetComboBoxPtr  combo_gap_axis;
	WidgetLabelPtr     lbl_gap_value;
	WidgetEditLinePtr  edit_gap_value;
	WidgetButtonPtr    btn_apply_gap;
	WidgetLabelPtr     label_status;

	Vector<NodePtr>    cached_nodes;

	// --- Static callbacks ---
	static void on_refresh_static()     { if (g_instance) g_instance->on_refresh(); }
	static void on_select_all_static()  { if (g_instance) g_instance->on_select_all(); }
	static void on_select_none_static() { if (g_instance) g_instance->on_select_none(); }
	static void on_apply_static()       { if (g_instance) g_instance->on_apply(); }
	static void on_scale_down_static()  { if (g_instance) g_instance->on_scale_down(); }
	static void on_apply_offset_static(){ if (g_instance) g_instance->on_apply_offset(); }
	static void on_filter_changed_static(){ if (g_instance) g_instance->on_refresh(); }
	static void on_apply_label_gap_static(){ if (g_instance) g_instance->on_apply_label_gap(); }

	// --- Refresh node list ---
	void on_refresh()
	{
		cached_nodes.clear();
		listbox_nodes->clear();

		Editor *editor = Editor::get();
		if (editor && editor->isLoaded())
		{
			int num = editor->getNumNodes();
			for (int i = 0; i < num; i++)
			{
				NodePtr n = editor->getNode(i);
				if (!n) continue;
				int filter = combo_filter ? combo_filter->getCurrentItem() : FILTER_ALL;
				if (node_matches_filter(n, filter))
				{
					cached_nodes.append(n);
					char buf[512];
					snprintf(buf, sizeof(buf), "[%s] %s",
							 n->getTypeName(), n->getName());
					listbox_nodes->addItem(buf);
				}
			}
		}

		if (cached_nodes.size() == 0)
		{
			World *world = World::get();
			if (world)
			{
				Vector<NodePtr> world_nodes;
				world->getNodes(world_nodes);
				for (int i = 0; i < world_nodes.size(); i++)
				{
					NodePtr n = world_nodes[i];
					if (!n) continue;
					int filter = combo_filter ? combo_filter->getCurrentItem() : FILTER_ALL;
					if (!node_matches_filter(n, filter)) continue;
					cached_nodes.append(n);
					char buf[512];
					snprintf(buf, sizeof(buf), "[%s] %s",
							 n->getTypeName(), n->getName());
					listbox_nodes->addItem(buf);
				}
			}
		}

		char buf[128];
		snprintf(buf, sizeof(buf), "Loaded %d nodes. Select nodes to scale.", cached_nodes.size());
		label_status->setText(buf);
		Log::message("PivotScaleTool: %s\n", buf);
	}

	// --- Select All / None ---
	void on_select_all()
	{
		for (int i = 0; i < listbox_nodes->getNumItems(); i++)
			listbox_nodes->setItemSelected(i, 1);
	}

	void on_select_none()
	{
		listbox_nodes->clearSelection();
	}

	// --- Apply ---
	void on_apply()
	{
		// Parse scale value
		float scale_value = 1.0f;
		if (edit_scale)
		{
			const char *text = edit_scale->getText();
			if (text && text[0] != '\0')
			{
				scale_value = static_cast<float>(atof(text));
				if (scale_value <= 0.0f)
				{
					Log::error("PivotScaleTool: scale must be > 0. Got %f\n", scale_value);
					label_status->setText("Error: scale must be > 0.");
					scale_value = 1.0f;
					edit_scale->setText("1.0");
					return;
				}
			}
		}

		// Get scale origin mode
		int origin_mode = combo_origin ? combo_origin->getCurrentItem() : 0;

		// Collect selected nodes from listbox
		int num_selected = listbox_nodes->getNumSelectedItems();
		if (num_selected == 0)
		{
			label_status->setText("No nodes selected in list. Select nodes first.");
			Log::message("PivotScaleTool: no nodes selected in list.\n");
			return;
		}

		Log::message("PivotScaleTool: applying scale=%.4f, origin=%d to %d node(s)\n",
					 scale_value, origin_mode, num_selected);

		int processed = 0;

		for (int s = 0; s < num_selected; s++)
		{
			int idx = listbox_nodes->getSelectedItem(s);
			if (idx < 0 || idx >= cached_nodes.size())
				continue;

			NodePtr node = cached_nodes[idx];
			if (!node)
				continue;

			switch (origin_mode)
			{
			case SCALE_FROM_PIVOT:
				scale_node_from_pivot(node, scale_value);
				break;
			case SCALE_FROM_BBOX:
				scale_node_from_bbox(node, scale_value);
				break;
			case SCALE_FROM_WORLD:
				scale_node_from_world_origin(node, scale_value);
				break;
			default:
				scale_node_from_pivot(node, scale_value);
				break;
			}

			Log::message("PivotScaleTool: scaled \"%s\" (origin=%d)\n",
						 node->getName(), origin_mode);
			processed++;
		}

		char buf[256];
		snprintf(buf, sizeof(buf), "Done: %d / %d nodes scaled (from %s).",
				 processed, num_selected,
				 origin_mode == SCALE_FROM_PIVOT ? "Pivot" :
				 origin_mode == SCALE_FROM_BBOX  ? "BBox Center" : "World Origin");
		label_status->setText(buf);
		Log::message("PivotScaleTool: %s\n", buf);
	}

	// --- Scale Down (1 / scale) ---
	void on_scale_down()
	{
		float scale_value = 1.0f;
		if (edit_scale)
		{
			const char *text = edit_scale->getText();
			if (text && text[0] != '\0')
			{
				scale_value = static_cast<float>(atof(text));
				if (scale_value <= 0.0f)
				{
					label_status->setText("Error: scale must be > 0.");
					return;
				}
			}
		}

		float inv_scale = 1.0f / scale_value;
		int origin_mode = combo_origin ? combo_origin->getCurrentItem() : 0;

		int num_selected = listbox_nodes->getNumSelectedItems();
		if (num_selected == 0)
		{
			label_status->setText("No nodes selected.");
			return;
		}

		int processed = 0;
		for (int s = 0; s < num_selected; s++)
		{
			int idx = listbox_nodes->getSelectedItem(s);
			if (idx < 0 || idx >= cached_nodes.size()) continue;
			NodePtr node = cached_nodes[idx];
			if (!node) continue;

			switch (origin_mode)
			{
			case SCALE_FROM_PIVOT: scale_node_from_pivot(node, inv_scale); break;
			case SCALE_FROM_BBOX:  scale_node_from_bbox(node, inv_scale); break;
			case SCALE_FROM_WORLD: scale_node_from_world_origin(node, inv_scale); break;
			default: scale_node_from_pivot(node, inv_scale); break;
			}
			processed++;
		}

		char buf[256];
		snprintf(buf, sizeof(buf), "Scaled down %d nodes (x%.4f).", processed, inv_scale);
		label_status->setText(buf);
		Log::message("PivotScaleTool: %s\n", buf);
	}

	// --- Apply Offset ---
	void on_apply_offset()
	{
		float ox = edit_offset_x ? static_cast<float>(atof(edit_offset_x->getText())) : 0.0f;
		float oy = edit_offset_y ? static_cast<float>(atof(edit_offset_y->getText())) : 0.0f;
		float oz = edit_offset_z ? static_cast<float>(atof(edit_offset_z->getText())) : 0.0f;

		int num_selected = listbox_nodes->getNumSelectedItems();
		if (num_selected == 0)
		{
			label_status->setText("No nodes selected.");
			return;
		}

		int processed = 0;
		for (int s = 0; s < num_selected; s++)
		{
			int idx = listbox_nodes->getSelectedItem(s);
			if (idx < 0 || idx >= cached_nodes.size()) continue;
			NodePtr node = cached_nodes[idx];
			if (!node) continue;

			Vec3 pos = node->getWorldPosition();
			pos.x += ox;
			pos.y += oy;
			pos.z += oz;
			node->setWorldPosition(pos);

			Log::message("PivotScaleTool: offset \"%s\" by (%.2f, %.2f, %.2f)\n",
						 node->getName(), ox, oy, oz);
			processed++;
		}

		char buf[256];
		snprintf(buf, sizeof(buf), "Offset %d nodes by (%.2f, %.2f, %.2f).",
				 processed, ox, oy, oz);
		label_status->setText(buf);
		Log::message("PivotScaleTool: %s\n", buf);
	}

	// --- Apply Label Gap ---
	// Finds all parents that have children named _Label1 and _Label2,
	// then moves _Label2 away from _Label1 by the gap distance along the chosen axis.
	void on_apply_label_gap()
	{
		float gap = edit_gap_value ? static_cast<float>(atof(edit_gap_value->getText())) : 1.0f;
		int axis = combo_gap_axis ? combo_gap_axis->getCurrentItem() : 0; // 0=Z, 1=Y, 2=X

		// Collect all world nodes
		Vector<NodePtr> all_nodes;
		World *world = World::get();
		if (world)
			world->getNodes(all_nodes);

		if (all_nodes.size() == 0)
		{
			label_status->setText("No nodes in world.");
			return;
		}

		int pairs_fixed = 0;

		for (int i = 0; i < all_nodes.size(); i++)
		{
			NodePtr parent = all_nodes[i];
			if (!parent) continue;

			// Look for _Label1 and _Label2 as direct children
			NodePtr label1;
			NodePtr label2;

			int num_children = parent->getNumChildren();
			for (int c = 0; c < num_children; c++)
			{
				NodePtr child = parent->getChild(c);
				if (!child) continue;
				const char *name = child->getName();
				if (!name) continue;

				if (strcmp(name, "_Label1") == 0) label1 = child;
				else if (strcmp(name, "_Label2") == 0) label2 = child;
			}

			if (!label1 || !label2) continue;

			// Get _Label1 position (local to parent)
			Vec3 pos1 = label1->getPosition();
			Vec3 pos2 = label2->getPosition();

			// Set _Label2 position = _Label1 position + gap along chosen axis
			Vec3 new_pos2 = pos1;
			switch (axis)
			{
			case 0: new_pos2.z += gap; break; // Z
			case 1: new_pos2.y += gap; break; // Y
			case 2: new_pos2.x += gap; break; // X
			}
			label2->setPosition(new_pos2);

			Log::message("PivotScaleTool: gap set for parent \"%s\" : _Label2 moved from (%.2f,%.2f,%.2f) to (%.2f,%.2f,%.2f)\n",
						 parent->getName(),
						 pos2.x, pos2.y, pos2.z,
						 new_pos2.x, new_pos2.y, new_pos2.z);
			pairs_fixed++;
		}

		char buf[256];
		snprintf(buf, sizeof(buf), "Label gap applied to %d parent(s). Gap=%.2f on %s axis.",
				 pairs_fixed, gap,
				 axis == 0 ? "Z" : axis == 1 ? "Y" : "X");
		label_status->setText(buf);
		Log::message("PivotScaleTool: %s\n", buf);
	}
};

// ---------------------------------------------------------------------------
// DLL entry points — extern_plugin interface for Unigine 2.6
// ---------------------------------------------------------------------------

#ifdef _WIN32
	#define UNIGINE_EXPORT __declspec(dllexport)
#else
	#define UNIGINE_EXPORT __attribute__((visibility("default")))
#endif

extern "C"
{

UNIGINE_EXPORT void *CreatePlugin()
{
	PivotScaleToolPlugin *p = new PivotScaleToolPlugin();
	g_instance = p;
	return p;
}

UNIGINE_EXPORT void ReleasePlugin(void *plugin)
{
	g_instance = nullptr;
	delete static_cast<PivotScaleToolPlugin *>(plugin);
}

} // extern "C"
