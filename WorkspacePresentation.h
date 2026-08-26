#ifndef VITRUGEN_WORKSPACE_PRESENTATION_H
#define VITRUGEN_WORKSPACE_PRESENTATION_H

#include <string>
#include <vector>

enum class WorkspaceStatusTone {

	Neutral = 0,
	Ready,
	Warning,
	Transition,

	Caution
};

enum class WorkspacePanelLayout {
	Main = 0,

	// Layer-3 workspace control panel
	SubLayer
};

// Presentation-only geometry variants for the Layer-3 tool panel.
// Workspaces choose the semantic variant; ViewPort owns all coordinates.
enum class WorkspaceSubLayerPanelLayout {
	Automatic = 0,
	AssemblyPreview,
	AssemblyEditVolume0,
	AssemblyEditTargetVolume0,
	AssemblyEditTargetVolume1,
	AssemblyOffsetVolume0,
	AssemblyOffsetTargetVolume0,
	AssemblyOffsetTargetVolume1,
	AssemblyApply,
	MarchingCubesReport
};

struct WorkspacePanelRow {

	std::string label;
	std::string value;

	bool selectable = false;
	bool selected = false;
	bool subordinate = false;
	bool emphasized = false;

	WorkspaceStatusTone tone =
		WorkspaceStatusTone::Neutral;
};

struct WorkspacePanelSection {

	std::string heading;
	std::vector<WorkspacePanelRow> rows;
	std::vector<std::string> notes;
};

struct WorkspaceRuntimeStatus {
	bool visible = false;

	// Main GOLD-style runtime column
	std::string titleLine;
	std::string contextLine;
	std::string objectLine;
	std::string helpLine;

	// Optional right-side detail column
	bool auxiliaryVisible = false;

	std::string auxiliaryStatusLine;

	WorkspaceStatusTone auxiliaryStatusTone =
		WorkspaceStatusTone::Neutral;

	std::string auxiliaryReferenceLine;
	std::string auxiliaryTargetLine;

};

struct WorkspaceNodeTrackPresentation {
	bool visible = false;

	std::string activeNodeLabel;

	int activeNode = 0;
	int nodeCount = 0;
};

struct WorkspacePresentation {
	
	bool panelVisible = false;
	
	WorkspacePanelLayout panelLayout =
		WorkspacePanelLayout::Main;

	WorkspaceSubLayerPanelLayout subLayerPanelLayout =
		WorkspaceSubLayerPanelLayout::Automatic;

	std::string workspaceName;

	std::string layerLabel;
	std::string subLayerLabel;
	std::string panelContextLine;

	std::string statusLine;

	WorkspaceStatusTone statusTone =
		WorkspaceStatusTone::Neutral;

	bool statusBlink = false;

	std::vector<WorkspacePanelSection> sections;

	std::string footerLine1;
	std::string footerLine2;

	WorkspaceNodeTrackPresentation nodeTrack;

	WorkspaceRuntimeStatus runtimeStatus;
};


#endif
