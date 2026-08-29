#include "renderer_Euclid.h"
#include "Graph3DWorkspace.h"

#include <GL/glew.h>
#include <GL/freeglut.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using namespace std;
using namespace glm;

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

Graph3DWorkspace::Graph3DWorkspace() = default;
Graph3DWorkspace::~Graph3DWorkspace() {
}

bool Graph3DWorkspace::initialize(WorkspaceServices & services) {
    (void)services;
    return true;
}

void Graph3DWorkspace::enter(WorkspaceServices & services) {
    (void)services;
}

void Graph3DWorkspace::exit(WorkspaceServices & services) {
    (void)services;
}

void Graph3DWorkspace::update(const WorkspaceFrameContext & frame, WorkspaceServices & services) {

    (void)frame;
    (void)services;
}

void Graph3DWorkspace::render(const WorkspaceFrameContext & frame, WorkspaceServices & services) {

    (void)frame;
    if (!services.renderer) return;

    EuclidRenderer& renderer = *services.renderer;
    const int gridDim = renderer.getGridDimSize();
    const float boxSize = static_cast<float>(renderer.getSimBoxSize());

    if (gridDim <= 0 || boxSize <= 0.0f) return;

    const float halfBox = boxSize * 0.5f;
    const float cellSize = boxSize / static_cast<float>(gridDim);


    renderer.setGrid(
        ivec3(
            gridDim,
            gridDim,
            gridDim
        ),
        vec3(
            -halfBox,
            -halfBox,
            -halfBox
        ),
        vec3(
            cellSize,
            cellSize,
            cellSize
        )
    );

    renderer.setGridMode3D();
    renderer.setGridStyle(renderer.getGridMajorEvery(), false);

    renderer.setWorkspaceGridVisibility(
        true,   // boundary
        true,   // major grid
        false,  // minor grid
        true    // axes
    );

    renderer.displayGrid();
}

bool Graph3DWorkspace::handleInput(const WorkspaceInputEvent& input, WorkspaceServices& services) {
    if (!services.arbiter) return false;

    using Workspace = TheArbiter::WorkspaceId;

    switch (input.action) {

    // -----------------------------------------------------
    // GRAPH_3D <- LINKED_PARTICLES
    // -----------------------------------------------------
    case WorkspaceInputAction::Decrease:
        services.arbiter->setActiveWorkspace(Workspace::LINKED_PARTICLES_MCAD);
        return true;

    // -----------------------------------------------------
    // GRAPH_3D -> SINGLE_PARTICLE
    // -----------------------------------------------------
    case WorkspaceInputAction::Increase:
        services.arbiter->setActiveWorkspace(Workspace::SINGLE_PARTICLE_MCAD);
        return true;


        // Only one row exists.
    case WorkspaceInputAction::Previous:
    case WorkspaceInputAction::Next:
    case WorkspaceInputAction::Activate:
        return true;


    case WorkspaceInputAction::Back:
        services.arbiter->requestReturnToGlobalShell(TheArbiter::WorkspaceDomain::GRID_3D);
        return true;


    default:
        return false;
    }
}

WorkspacePresentation Graph3DWorkspace::buildPresentation() const {
    return buildLayer1Presentation();
}

WorkspacePresentation
Graph3DWorkspace::buildLayer1TransitionPresentation() const {
    return buildLayer1Presentation();
}

WorkspacePresentation Graph3DWorkspace::buildLayer1Presentation() const {

    WorkspacePresentation p;
    p.panelVisible = true;
    p.workspaceName ="LAYER 1 -> GRID_3D WORKSPACE CONFIGURATION";
    p.layerLabel = "MODE: GRAPH_3D";

    WorkspacePanelSection section;
    section.rows.push_back(
        makeRow(
            "[1]: GRID_3D SELECTION",
            "GRAPH_3D",
            true
        )
    );

    p.sections.push_back(section);
    p.statusLine = "GRAPH_3D is reserved for the next pass.";
    p.statusTone = WorkspaceStatusTone::Warning;
    p.footerLine1 = "W / S: Select list     A / D: Change value";
    p.footerLine2 = "E: Activate selected row     Q: Back one layer";

    return p;

}
