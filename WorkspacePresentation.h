#ifndef VITRUGEN_WORKSPACE_PRESENTATION_H
#define VITRUGEN_WORKSPACE_PRESENTATION_H

#include <string>
#include <vector>

enum class WorkspaceStatusTone {

	Neutral = 0,
	Ready,
	Warning,

	Transition
};

struct WorkspacePanelRow {

	std::string label;
	std::string value;

	bool selectable = false;
	bool selected = false;
	bool subordinate = false;
	bool emphasized = false;
};

struct WorkspacePanelSection {

	std::string heading;
	std::vector<WorkspacePanelRow> rows;
};

struct WorkspacePresentation {
	
	bool panelVisible = false;
	
	std::string workspaceName;

	std::string layerLabel;
	std::string subLayerLabel;

	std::string statusLine;

	WorkspaceStatusTone statusTone =
		WorkspaceStatusTone::Neutral;

	bool statusBlink = false;

	std::vector<WorkspacePanelSection> sections;

	std::string footerLine1;
	std::string footerLine2;
};

#endif
