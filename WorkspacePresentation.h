#ifndef VITRUGEN_WORKSPACE_PRESENTATION_H
#define VITRUGEN_WORKSPACE_PRESENTATION_H

#include <string>
#include <vector>

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

	std::vector<WorkspacePanelSection> sections;

	std::string footerLine1;
	std::string footerLine2;
};

#endif
