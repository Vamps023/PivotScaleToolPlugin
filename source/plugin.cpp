/**
 * PivotScaleTool — Unigine 2.6 Plugin (DLL)
 * Version: 1.0.0
 * Author: Swapnil Nare
 * Year: 2026
 *
 * Features:
 *   - Scale Origin: Pivot Center / BBox Center / World Origin
 *   - Scale Up & Scale Down operations
 *   - Node type filtering (MeshDynamic, MeshStatic, Lights, etc.)
 *   - Position offset controls (X, Y, Z)
 *   - Label Gap for _Label1/_Label2 sibling pairs
 *   - Scrollable node list with multi-select
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
#include <cstring>

using namespace Unigine;
using namespace Unigine::Math;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
namespace PluginInfo
{
	static const char *NAME    = "PivotScaleTool";
	static const char *VERSION = "1.0.0";
}

namespace UI
{
	static const int WINDOW_X      = 50;
	static const int WINDOW_Y      = 50;
	static const int WINDOW_WIDTH  = 280;
	static const int WINDOW_HEIGHT = 420;
	static const int WIDGET_WIDTH  = 260;
	static const int LIST_HEIGHT   = 200;
	static const int PADDING       = 2;
	static const int SPACING       = 2;

	static const char *DEFAULT_SCALE  = "2.0";
	static const char *DEFAULT_OFFSET = "0.0";
	static const char *DEFAULT_GAP    = "1.0";
}

namespace Labels
{
	static const char *SCALE_ORIGIN   = "Scale From:";
	static const char *UNIFORM_SCALE  = "Uniform Scale:";
	static const char *OFFSET_HEADER  = "Position Offset (X, Y, Z):";
	static const char *FILTER_HEADER  = "Show Node Type:";
	static const char *NODES_HEADER   = "Nodes (Ctrl+Click = multi-select):";
	static const char *GAP_HEADER     = "Label Gap (_Label1 / _Label2):";
	static const char *GAP_AXIS       = "Gap Axis:";
	static const char *GAP_DISTANCE   = "Gap Distance:";
	static const char *STATUS_INIT    = "Click 'Refresh Nodes' to start.";

	static const char *ORIGIN_PIVOT  = "Pivot Center";
	static const char *ORIGIN_BBOX   = "BBox Center";
	static const char *ORIGIN_WORLD  = "World Origin";

	static const char *GAP_AXIS_Z = "Z (Up/Down)";
	static const char *GAP_AXIS_Y = "Y (Forward/Back)";
	static const char *GAP_AXIS_X = "X (Left/Right)";

	static const char *LABEL1_NAME = "_Label1";
	static const char *LABEL2_NAME = "_Label2";
}

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------
enum ScaleOrigin
{
	SCALE_FROM_PIVOT = 0,
	SCALE_FROM_BBOX  = 1,
	SCALE_FROM_WORLD = 2,
};

enum GapAxis
{
	GAP_AXIS_Z = 0,
	GAP_AXIS_Y = 1,
	GAP_AXIS_X = 2,
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

static const char *FILTER_NAMES[] = {
	"All Nodes",
	"ObjectMeshDynamic",
	"ObjectMeshStatic",
	"ObjectMeshSkinned",
	"ObjectMeshCluster",
	"ObjectMeshClutter",
	"All Objects",
	"Lights",
	"Decals",
	"Players",
};

static const int NUM_FILTERS = sizeof(FILTER_NAMES) / sizeof(FILTER_NAMES[0]);

// ---------------------------------------------------------------------------
// Utility functions
// ---------------------------------------------------------------------------

static bool nodeMatchesFilter(const NodePtr &n, int filter)
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
	case FILTER_LIGHTS:        return (t >= Node::LIGHT_BEGIN  && t <= Node::LIGHT_END);
	case FILTER_DECALS:        return (t >= Node::DECAL_BEGIN  && t <= Node::DECAL_END);
	case FILTER_PLAYERS:       return (t >= Node::PLAYER_BEGIN && t <= Node::PLAYER_END);
	default:                   return true;
	}
}

static float parseEditFloat(const WidgetEditLinePtr &edit, float fallback = 0.0f)
{
	if (!edit) return fallback;
	const char *text = edit->getText();
	if (!text || text[0] == '\0') return fallback;
	float val = static_cast<float>(atof(text));
	return val;
}

static bool isValidScale(float scale)
{
	return scale > 0.0f;
}

static const char *originName(int mode)
{
	switch (mode)
	{
	case SCALE_FROM_PIVOT: return Labels::ORIGIN_PIVOT;
	case SCALE_FROM_BBOX:  return Labels::ORIGIN_BBOX;
	case SCALE_FROM_WORLD: return Labels::ORIGIN_WORLD;
	default:               return Labels::ORIGIN_PIVOT;
	}
}

static const char *axisName(int axis)
{
	switch (axis)
	{
	case GAP_AXIS_Z: return "Z";
	case GAP_AXIS_Y: return "Y";
	case GAP_AXIS_X: return "X";
	default:         return "Z";
	}
}

static String formatNodeLabel(const NodePtr &n)
{
	return String::format("[%s] %s", n->getTypeName(), n->getName());
}

// ---------------------------------------------------------------------------
// Scale helpers
// ---------------------------------------------------------------------------

static bool computeBBoxCenter(const NodePtr &node, Vec3 &out_center)
{
	if (!node) return false;
	BoundBox bb = node->getBoundBox();
	if (!bb.isValid()) return false;
	out_center = Vec3((bb.getMin() + bb.getMax()) * 0.5f);
	return true;
}

static void scaleNodeFromPivot(const NodePtr &node, float scaleFactor)
{
	vec3 cur = node->getScale();
	node->setScale(cur * scaleFactor);
}

static void scaleNodeFromBBox(const NodePtr &node, float scaleFactor)
{
	Vec3 bboxCenter;
	if (!computeBBoxCenter(node, bboxCenter))
	{
		scaleNodeFromPivot(node, scaleFactor);
		return;
	}

	Mat4 t = node->getTransform();
	Vec3 pivotWorld  = Vec3(t.getColumn3(3));
	Vec3 centerWorld = Vec3(t * Vec4(bboxCenter, 1.0f));
	Vec3 newPivot    = centerWorld + (pivotWorld - centerWorld) * static_cast<double>(scaleFactor);

	t.setColumn3(3, newPivot);
	node->setTransform(t);

	vec3 cur = node->getScale();
	node->setScale(cur * scaleFactor);
}

static void scaleNodeFromWorldOrigin(const NodePtr &node, float scaleFactor)
{
	Mat4 t = node->getTransform();
	Vec3 pivotWorld = Vec3(t.getColumn3(3));
	Vec3 newPivot   = pivotWorld * static_cast<double>(scaleFactor);

	t.setColumn3(3, newPivot);
	node->setTransform(t);

	vec3 cur = node->getScale();
	node->setScale(cur * scaleFactor);
}

static void applyScaleToNode(const NodePtr &node, float scaleFactor, int originMode)
{
	switch (originMode)
	{
	case SCALE_FROM_PIVOT: scaleNodeFromPivot(node, scaleFactor);       break;
	case SCALE_FROM_BBOX:  scaleNodeFromBBox(node, scaleFactor);        break;
	case SCALE_FROM_WORLD: scaleNodeFromWorldOrigin(node, scaleFactor); break;
	default:               scaleNodeFromPivot(node, scaleFactor);       break;
	}
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

	virtual const char *get_name() override { return PluginInfo::NAME; }

	virtual int init() override
	{
		Log::message("%s v%s: initializing...\n", PluginInfo::NAME, PluginInfo::VERSION);

		GuiPtr gui = Gui::get();
		if (!gui)
		{
			Log::error("%s: failed to obtain Gui.\n", PluginInfo::NAME);
			return 0;
		}

		createWindow(gui);
		createScaleControls(gui);
		createOffsetControls(gui);
		createFilterControls(gui);
		createNodeList(gui);
		createActionButtons(gui);
		createLabelGapControls(gui);
		createStatusBar(gui);

		gui->addChild(window->getWidget(), Gui::ALIGN_OVERLAP);

		refreshNodeList();

		Log::message("%s: initialized successfully.\n", PluginInfo::NAME);
		return 1;
	}

	virtual int shutdown() override
	{
		Log::message("%s: shutting down...\n", PluginInfo::NAME);

		GuiPtr gui = Gui::get();
		if (gui && window)
			gui->removeChild(window->getWidget());

		cached_nodes.clear();
		releaseWidgets();

		Log::message("%s: shut down.\n", PluginInfo::NAME);
		return 1;
	}

private:
	// -----------------------------------------------------------------------
	// Widget members
	// -----------------------------------------------------------------------
	WidgetWindowPtr    window;
	WidgetVBoxPtr      vbox;

	// Scale controls
	WidgetLabelPtr     lbl_origin;
	WidgetComboBoxPtr  combo_origin;
	WidgetLabelPtr     lbl_scale;
	WidgetEditLinePtr  edit_scale;

	// Offset controls
	WidgetLabelPtr     lbl_offset;
	WidgetEditLinePtr  edit_offset_x;
	WidgetEditLinePtr  edit_offset_y;
	WidgetEditLinePtr  edit_offset_z;

	// Filter controls
	WidgetLabelPtr     lbl_filter;
	WidgetComboBoxPtr  combo_filter;

	// Node list
	WidgetLabelPtr     lbl_nodes;
	WidgetScrollBoxPtr scrollbox_nodes;
	WidgetListBoxPtr   listbox_nodes;

	// Action buttons
	WidgetButtonPtr    btn_refresh;
	WidgetButtonPtr    btn_select_all;
	WidgetButtonPtr    btn_select_none;
	WidgetButtonPtr    btn_scale_up;
	WidgetButtonPtr    btn_scale_down;
	WidgetButtonPtr    btn_apply_offset;

	// Label gap controls
	WidgetLabelPtr     lbl_label_gap;
	WidgetLabelPtr     lbl_gap_axis;
	WidgetComboBoxPtr  combo_gap_axis;
	WidgetLabelPtr     lbl_gap_value;
	WidgetEditLinePtr  edit_gap_value;
	WidgetButtonPtr    btn_apply_gap;

	// Status
	WidgetLabelPtr     label_status;

	// Data
	Vector<NodePtr>    cached_nodes;

	// -----------------------------------------------------------------------
	// UI creation helpers — keep init() clean
	// -----------------------------------------------------------------------
	void createWindow(const GuiPtr &gui)
	{
		window = WidgetWindow::create(gui, "Pivot & Scale Tool", 4, 4);
		window->setPosition(UI::WINDOW_X, UI::WINDOW_Y);
		window->setSizeable(1);
		window->getWidget()->setWidth(UI::WINDOW_WIDTH);
		window->getWidget()->setHeight(UI::WINDOW_HEIGHT);

		vbox = WidgetVBox::create(gui, UI::SPACING, UI::PADDING);
		window->addChild(vbox->getWidget(), Gui::ALIGN_EXPAND);
	}

	void addLabel(const GuiPtr &gui, WidgetLabelPtr &lbl, const char *text)
	{
		lbl = WidgetLabel::create(gui, text);
		vbox->addChild(lbl->getWidget(), Gui::ALIGN_EXPAND);
	}

	void addEditLine(const GuiPtr &gui, WidgetEditLinePtr &edit, const char *defaultText)
	{
		edit = WidgetEditLine::create(gui, defaultText);
		edit->getWidget()->setWidth(UI::WIDGET_WIDTH);
		vbox->addChild(edit->getWidget(), Gui::ALIGN_EXPAND);
	}

	void addButton(const GuiPtr &gui, WidgetButtonPtr &btn, const char *text, void (*callback)())
	{
		btn = WidgetButton::create(gui, text);
		btn->setCallback0(Gui::CLICKED, MakeCallback(callback));
		vbox->addChild(btn->getWidget(), Gui::ALIGN_EXPAND);
	}

	void createScaleControls(const GuiPtr &gui)
	{
		addLabel(gui, lbl_origin, Labels::SCALE_ORIGIN);

		combo_origin = WidgetComboBox::create(gui);
		combo_origin->addItem(Labels::ORIGIN_PIVOT);
		combo_origin->addItem(Labels::ORIGIN_BBOX);
		combo_origin->addItem(Labels::ORIGIN_WORLD);
		combo_origin->setCurrentItem(SCALE_FROM_PIVOT);
		combo_origin->getWidget()->setWidth(UI::WIDGET_WIDTH);
		vbox->addChild(combo_origin->getWidget(), Gui::ALIGN_EXPAND);

		addLabel(gui, lbl_scale, Labels::UNIFORM_SCALE);
		addEditLine(gui, edit_scale, UI::DEFAULT_SCALE);
	}

	void createOffsetControls(const GuiPtr &gui)
	{
		addLabel(gui, lbl_offset, Labels::OFFSET_HEADER);
		addEditLine(gui, edit_offset_x, UI::DEFAULT_OFFSET);
		addEditLine(gui, edit_offset_y, UI::DEFAULT_OFFSET);
		addEditLine(gui, edit_offset_z, UI::DEFAULT_OFFSET);
	}

	void createFilterControls(const GuiPtr &gui)
	{
		addLabel(gui, lbl_filter, Labels::FILTER_HEADER);

		combo_filter = WidgetComboBox::create(gui);
		for (int i = 0; i < NUM_FILTERS; i++)
			combo_filter->addItem(FILTER_NAMES[i]);
		combo_filter->setCurrentItem(FILTER_MESH_DYNAMIC);
		combo_filter->setCallback0(Gui::CHANGED, MakeCallback(onFilterChangedStatic));
		vbox->addChild(combo_filter->getWidget(), Gui::ALIGN_EXPAND);
	}

	void createNodeList(const GuiPtr &gui)
	{
		addLabel(gui, lbl_nodes, Labels::NODES_HEADER);

		scrollbox_nodes = WidgetScrollBox::create(gui, UI::SPACING, UI::PADDING);
		scrollbox_nodes->getWidget()->setHeight(UI::LIST_HEIGHT);
		scrollbox_nodes->setVScrollEnabled(1);
		scrollbox_nodes->setHScrollEnabled(0);
		scrollbox_nodes->setBorder(1);
		vbox->addChild(scrollbox_nodes->getWidget(), Gui::ALIGN_EXPAND);

		listbox_nodes = WidgetListBox::create(gui);
		listbox_nodes->setMultiSelection(1);
		scrollbox_nodes->addChild(listbox_nodes->getWidget(), Gui::ALIGN_EXPAND);
	}

	void createActionButtons(const GuiPtr &gui)
	{
		addButton(gui, btn_refresh,      "Refresh Nodes", onRefreshStatic);
		addButton(gui, btn_select_all,   "Select All",    onSelectAllStatic);
		addButton(gui, btn_select_none,  "Select None",   onSelectNoneStatic);
		addButton(gui, btn_scale_up,     "Scale Up",      onScaleUpStatic);
		addButton(gui, btn_scale_down,   "Scale Down",    onScaleDownStatic);
		addButton(gui, btn_apply_offset, "Apply Offset",  onApplyOffsetStatic);
	}

	void createLabelGapControls(const GuiPtr &gui)
	{
		addLabel(gui, lbl_label_gap, Labels::GAP_HEADER);
		addLabel(gui, lbl_gap_axis,  Labels::GAP_AXIS);

		combo_gap_axis = WidgetComboBox::create(gui);
		combo_gap_axis->addItem(Labels::GAP_AXIS_Z);
		combo_gap_axis->addItem(Labels::GAP_AXIS_Y);
		combo_gap_axis->addItem(Labels::GAP_AXIS_X);
		combo_gap_axis->setCurrentItem(GAP_AXIS_Z);
		vbox->addChild(combo_gap_axis->getWidget(), Gui::ALIGN_EXPAND);

		addLabel(gui, lbl_gap_value, Labels::GAP_DISTANCE);
		addEditLine(gui, edit_gap_value, UI::DEFAULT_GAP);
		addButton(gui, btn_apply_gap, "Apply Label Gap", onApplyLabelGapStatic);
	}

	void createStatusBar(const GuiPtr &gui)
	{
		addLabel(gui, label_status, Labels::STATUS_INIT);
	}

	void releaseWidgets()
	{
		label_status     = WidgetLabelPtr();
		lbl_origin       = WidgetLabelPtr();
		lbl_scale        = WidgetLabelPtr();
		lbl_offset       = WidgetLabelPtr();
		lbl_filter       = WidgetLabelPtr();
		combo_filter     = WidgetComboBoxPtr();
		lbl_nodes        = WidgetLabelPtr();
		btn_scale_up     = WidgetButtonPtr();
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
	}

	// -----------------------------------------------------------------------
	// Static callbacks — bridge to instance methods
	// -----------------------------------------------------------------------
	static void onRefreshStatic()        { if (g_instance) g_instance->refreshNodeList(); }
	static void onSelectAllStatic()      { if (g_instance) g_instance->selectAll(); }
	static void onSelectNoneStatic()     { if (g_instance) g_instance->selectNone(); }
	static void onScaleUpStatic()        { if (g_instance) g_instance->scaleSelected(false); }
	static void onScaleDownStatic()      { if (g_instance) g_instance->scaleSelected(true); }
	static void onApplyOffsetStatic()    { if (g_instance) g_instance->applyOffset(); }
	static void onFilterChangedStatic()  { if (g_instance) g_instance->refreshNodeList(); }
	static void onApplyLabelGapStatic()  { if (g_instance) g_instance->applyLabelGap(); }

	// -----------------------------------------------------------------------
	// Status helper
	// -----------------------------------------------------------------------
	void setStatus(const char *msg)
	{
		label_status->setText(msg);
		Log::message("%s: %s\n", PluginInfo::NAME, msg);
	}

	void setStatus(const String &msg)
	{
		setStatus(msg.get());
	}

	// -----------------------------------------------------------------------
	// Selected node iteration helper
	// Returns number of selected items, or 0 with status set.
	// -----------------------------------------------------------------------
	int getSelectedCount()
	{
		int n = listbox_nodes->getNumSelectedItems();
		if (n == 0)
			setStatus("No nodes selected.");
		return n;
	}

	NodePtr getSelectedNode(int selectionIndex)
	{
		int idx = listbox_nodes->getSelectedItem(selectionIndex);
		if (idx < 0 || idx >= cached_nodes.size()) return NodePtr();
		return cached_nodes[idx];
	}

	int getCurrentFilter()
	{
		return combo_filter ? combo_filter->getCurrentItem() : FILTER_ALL;
	}

	int getCurrentOriginMode()
	{
		return combo_origin ? combo_origin->getCurrentItem() : SCALE_FROM_PIVOT;
	}

	// -----------------------------------------------------------------------
	// Node collection — tries Editor first, then World fallback
	// -----------------------------------------------------------------------
	void collectFilteredNodes(int filter)
	{
		Editor *editor = Editor::get();
		if (editor && editor->isLoaded())
		{
			int num = editor->getNumNodes();
			for (int i = 0; i < num; i++)
			{
				NodePtr n = editor->getNode(i);
				if (!n) continue;
				if (!nodeMatchesFilter(n, filter)) continue;
				cached_nodes.append(n);
				listbox_nodes->addItem(formatNodeLabel(n).get());
			}
		}

		if (cached_nodes.size() == 0)
		{
			World *world = World::get();
			if (!world) return;
			Vector<NodePtr> worldNodes;
			world->getNodes(worldNodes);
			for (int i = 0; i < worldNodes.size(); i++)
			{
				NodePtr n = worldNodes[i];
				if (!n) continue;
				if (!nodeMatchesFilter(n, filter)) continue;
				cached_nodes.append(n);
				listbox_nodes->addItem(formatNodeLabel(n).get());
			}
		}
	}

	// -----------------------------------------------------------------------
	// Actions
	// -----------------------------------------------------------------------
	void refreshNodeList()
	{
		cached_nodes.clear();
		listbox_nodes->clear();

		collectFilteredNodes(getCurrentFilter());

		setStatus(String::format("Loaded %d nodes. Select nodes to scale.", cached_nodes.size()));
	}

	void selectAll()
	{
		for (int i = 0; i < listbox_nodes->getNumItems(); i++)
			listbox_nodes->setItemSelected(i, 1);
	}

	void selectNone()
	{
		listbox_nodes->clearSelection();
	}

	void scaleSelected(bool invert)
	{
		float scaleValue = parseEditFloat(edit_scale, 1.0f);
		if (!isValidScale(scaleValue))
		{
			setStatus("Error: scale must be > 0.");
			Log::error("%s: invalid scale value %.4f\n", PluginInfo::NAME, scaleValue);
			return;
		}

		float factor = invert ? (1.0f / scaleValue) : scaleValue;
		int originMode = getCurrentOriginMode();
		int numSelected = getSelectedCount();
		if (numSelected == 0) return;

		int processed = 0;
		for (int s = 0; s < numSelected; s++)
		{
			NodePtr node = getSelectedNode(s);
			if (!node) continue;
			applyScaleToNode(node, factor, originMode);
			Log::message("%s: scaled \"%s\" x%.4f (from %s)\n",
						 PluginInfo::NAME, node->getName(), factor, originName(originMode));
			processed++;
		}

		String msg = invert
			? String::format("Scaled down %d nodes (x%.4f from %s).", processed, factor, originName(originMode))
			: String::format("Scaled up %d / %d nodes (x%.4f from %s).", processed, numSelected, factor, originName(originMode));
		setStatus(msg);
	}

	void applyOffset()
	{
		float ox = parseEditFloat(edit_offset_x);
		float oy = parseEditFloat(edit_offset_y);
		float oz = parseEditFloat(edit_offset_z);

		int numSelected = getSelectedCount();
		if (numSelected == 0) return;

		int processed = 0;
		for (int s = 0; s < numSelected; s++)
		{
			NodePtr node = getSelectedNode(s);
			if (!node) continue;

			Vec3 pos = node->getWorldPosition();
			pos.x += ox;
			pos.y += oy;
			pos.z += oz;
			node->setWorldPosition(pos);

			Log::message("%s: offset \"%s\" by (%.2f, %.2f, %.2f)\n",
						 PluginInfo::NAME, node->getName(), ox, oy, oz);
			processed++;
		}

		setStatus(String::format("Offset %d nodes by (%.2f, %.2f, %.2f).", processed, ox, oy, oz));
	}

	void applyLabelGap()
	{
		float gap = parseEditFloat(edit_gap_value, 1.0f);
		int axis  = combo_gap_axis ? combo_gap_axis->getCurrentItem() : GAP_AXIS_Z;

		Vector<NodePtr> allNodes;
		World *world = World::get();
		if (world) world->getNodes(allNodes);

		if (allNodes.size() == 0)
		{
			setStatus("No nodes in world.");
			return;
		}

		int pairsFixed = 0;

		for (int i = 0; i < allNodes.size(); i++)
		{
			NodePtr parent = allNodes[i];
			if (!parent) continue;

			NodePtr label1, label2;
			int numChildren = parent->getNumChildren();
			for (int c = 0; c < numChildren; c++)
			{
				NodePtr child = parent->getChild(c);
				if (!child) continue;
				const char *name = child->getName();
				if (!name) continue;

				if (strcmp(name, Labels::LABEL1_NAME) == 0) label1 = child;
				else if (strcmp(name, Labels::LABEL2_NAME) == 0) label2 = child;
			}

			if (!label1 || !label2) continue;

			Vec3 pos1 = label1->getPosition();
			Vec3 pos2 = label2->getPosition();

			Vec3 newPos2 = pos1;
			switch (axis)
			{
			case GAP_AXIS_Z: newPos2.z += gap; break;
			case GAP_AXIS_Y: newPos2.y += gap; break;
			case GAP_AXIS_X: newPos2.x += gap; break;
			}
			label2->setPosition(newPos2);

			Log::message("%s: gap set for parent \"%s\" : _Label2 (%.2f,%.2f,%.2f) -> (%.2f,%.2f,%.2f)\n",
						 PluginInfo::NAME, parent->getName(),
						 pos2.x, pos2.y, pos2.z,
						 newPos2.x, newPos2.y, newPos2.z);
			pairsFixed++;
		}

		setStatus(String::format("Label gap applied to %d parent(s). Gap=%.2f on %s axis.",
				  pairsFixed, gap, axisName(axis)));
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
