#include "renderer_Euclid.h"
#include "Graph2DWorkspace.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

using namespace std;

namespace {
    WorkspacePanelRow makeRow(
        const string& label,
        const string& value,
        bool selected,
        bool selectable = true) {

        WorkspacePanelRow row;
        row.label = label;
        row.value = value;
        row.selectable = selectable;
        row.selected = selected;
        return row;
    }

    template <typename Enum>
    Enum cycleEnum(Enum value, int count, int direction) {
        const int step = direction < 0 ? -1 : 1;
        const int current = static_cast<int>(value);
        return static_cast<Enum>((current + step + count) % count);
    }
}

Graph2DWorkspace::Graph2DWorkspace() = default;
Graph2DWorkspace::~Graph2DWorkspace() {
}

bool Graph2DWorkspace::initialize(WorkspaceServices& services) {
    (void)services;
    return true;
}

void Graph2DWorkspace::enter(WorkspaceServices& services) {
    (void)services;
}

void Graph2DWorkspace::exit(WorkspaceServices& services) {
    (void)services;
}

void Graph2DWorkspace::update(
    const WorkspaceFrameContext& frame,
    WorkspaceServices& services) {

    (void)frame;
    (void)services;
}

void Graph2DWorkspace::render(
    const WorkspaceFrameContext& frame,
    WorkspaceServices& services) {

    (void)frame;
    (void)services;
}

bool Graph2DWorkspace::handleInput(
    const WorkspaceInputEvent& input,
    WorkspaceServices& services) {

    if (!services.arbiter)
        return false;

    switch (input.action) {

    case WorkspaceInputAction::Decrease:
    case WorkspaceInputAction::Increase:
        services.arbiter->setActiveWorkspace(TheArbiter::WorkspaceId::TEXTURE_MAP_2D);
        return true;


    case WorkspaceInputAction::Back:
        services.arbiter->requestReturnToGlobalShell(TheArbiter::WorkspaceDomain::GRID_2D);

        return true;

    default:
        return false;
    }
}

WorkspacePresentation Graph2DWorkspace::buildPresentation() const {
    return buildLayer1Presentation();
}

WorkspacePresentation 
Graph2DWorkspace::buildLayer1TransitionPresentation() const {
    return buildLayer1Presentation();
}

WorkspacePresentation Graph2DWorkspace::buildLayer1Presentation() const {

    WorkspacePresentation p;
    p.panelVisible = true;

    p.workspaceName = "LAYER 1 -> GRID_2D WORKSPACE CONFIGURATION";
    p.layerLabel = "MODE: GRAPH_2D";

    WorkspacePanelSection section;
    section.rows.push_back(
        makeRow(
            "[1]: GRID_2D SELECTION",
            "GRAPH_2D",
            true
        )
    );

    p.sections.push_back(section);
    p.statusLine = "GRAPH_2D is reserved for the next pass.";
    p.statusTone = WorkspaceStatusTone::Warning;
    p.footerLine1 = "W / S: Select list     A / D: Change value";
    p.footerLine2 = "E: Activate selected row     Q: Back one layer";

    return p;

}
