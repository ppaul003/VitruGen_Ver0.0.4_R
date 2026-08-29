#include "renderer_Euclid.h"
#include "LinkedParticlesWorkspace.h"

#include <GL/glew.h>
#include <GL/freeglut.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using namespace std;
using namespace glm;

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

LinkedParticlesWorkspace::LinkedParticlesWorkspace() = default;
LinkedParticlesWorkspace::~LinkedParticlesWorkspace() {
}

bool LinkedParticlesWorkspace::initialize(WorkspaceServices & services) {
    (void)services;
    return true;
}

void LinkedParticlesWorkspace::enter(WorkspaceServices & services) {
    (void)services;
}

void LinkedParticlesWorkspace::exit(WorkspaceServices & services) {
    (void)services;
}

void LinkedParticlesWorkspace::update(const WorkspaceFrameContext & frame, WorkspaceServices & services) {
    (void)frame;
    (void)services;
}

void LinkedParticlesWorkspace::render(const WorkspaceFrameContext & frame, WorkspaceServices & services) {

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

bool LinkedParticlesWorkspace::handleInput(const WorkspaceInputEvent & input, WorkspaceServices & services) {
    if (!services.arbiter) return false;

    using Workspace = TheArbiter::WorkspaceId;

    switch (input.action) {

    // -----------------------------------------------------
    // LINKED_PARTICLES <- SINGLE_PARTICLE
    // -----------------------------------------------------
    case WorkspaceInputAction::Decrease:
        services.arbiter->setActiveWorkspace(Workspace::SINGLE_PARTICLE_MCAD);
        return true;

    case WorkspaceInputAction::Increase:
        services.arbiter->setActiveWorkspace(TheArbiter::WorkspaceId::GRAPH_3D);
        return true;


    case WorkspaceInputAction::Back:
        services.arbiter->requestReturnToGlobalShell(TheArbiter::WorkspaceDomain::GRID_3D);
        return true;

    default:
        return false;
    }
}

WorkspacePresentation LinkedParticlesWorkspace::buildPresentation() const {
    return buildLayer1Presentation();
}

WorkspacePresentation
LinkedParticlesWorkspace::buildLayer1TransitionPresentation() const {
    return buildLayer1Presentation();
}

WorkspacePresentation LinkedParticlesWorkspace::buildLayer1Presentation() const {

    WorkspacePresentation p;
    p.panelVisible = true;
    p.workspaceName = "LAYER 1 -> GRID_3D WORKSPACE CONFIGURATION";
    p.layerLabel = "MODE: LINKED_PARTICLES";

    WorkspacePanelSection section;
    section.rows.push_back(
        makeRow(
            "[1]: GRID_3D SELECTION",
            "LINKED_PARTICLES",
            true
        )
    );

    p.sections.push_back(section);
    p.statusLine = "LINKED_PARTICLES is reserved for the next pass.";
    p.statusTone = WorkspaceStatusTone::Warning;
    p.footerLine1 = "W / S: Select list     A / D: Change value";
    p.footerLine2 = "E: Activate selected row     Q: Back one layer";

    return p;

}