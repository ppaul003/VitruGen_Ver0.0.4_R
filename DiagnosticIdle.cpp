#include "DiagnosticIdle.h"
#include "renderer_Euclid.h"

#include <cmath>
#include <algorithm>

using namespace std;
using namespace glm;

bool DiagnosticIdle::initialize(
	WorkspaceServices& services) {

	return services.renderer != nullptr;
}

void DiagnosticIdle::enter(WorkspaceServices& services) {

	m_sliceCycle = 0.0f;
}

void DiagnosticIdle::exit(
	WorkspaceServices& services) {
}

void DiagnosticIdle::update(
	const WorkspaceFrameContext& frame,
	WorkspaceServices& services) {

	// ---------------------------------------------------------
	// GOLD Ver004 idle slice animation:
	//
	//     0 -> 1 : XY
	//     1 -> 2 : XZ
	//     2 -> 3 : YZ
	//
	// 0.35 cycle units / second.
	//
	// Full XYZ sequence:
	//     3 / 0.35 ~= 8.57 seconds
	// ---------------------------------------------------------
	m_sliceCycle = fmod(frame.elapsedTime * 0.35f, 3.0f);

	if (m_sliceCycle < 0.0f) {

		m_sliceCycle += 3.0f;
	}
}

WorkspacePresentation
DiagnosticIdle::buildPresentation() const {

	WorkspacePresentation presentation;

	presentation.panelVisible = true;

	presentation.workspaceName =
		"VITRUGEN HOST DIAGNOSTIC";

	presentation.layerLabel =
		"GLOBAL SHELL";

	presentation.subLayerLabel =
		"DIAGNOSTIC IDLE";

	presentation.statusLine =
		"WORKSPACE SOCKET ONLINE";

	presentation.footerLine1 =
		"VitruGen_Ver004_R";

	return presentation;
}

void DiagnosticIdle::render(
	const WorkspaceFrameContext& frame,
	WorkspaceServices& services) {

	if (!services.renderer)
		return;

	EuclidRenderer& renderer = *services.renderer;

	EuclidRenderer::UniformGrid grid;

	constexpr int kIdleGridDim = 64;
	constexpr int kIdleMajorEvery = 8;

	const float boxSize = renderer.getSimBoxSize();
	const float halfBox = boxSize * 0.5f;

	grid.dimensions =
		ivec3(
			kIdleGridDim, 
			kIdleGridDim, 
			kIdleGridDim
		);

	grid.origin = vec3(-halfBox);

	float cellSize = boxSize / 
		static_cast<float>(kIdleGridDim);

	grid.cellSize = vec3(cellSize);
	grid.majorEvery = kIdleMajorEvery;

	EuclidRenderer::GridDisplay display;
	display.boundary = true;
	display.majorGrid = true;
	display.minorGrid = false;
	display.axes = true;

	renderer.drawUniformGrid(grid, display);

	const int segment =
		std::min(2, static_cast<int>(m_sliceCycle));

	const float local = m_sliceCycle -
		static_cast<float>(segment);

	// ---------------------------------------------------------
	// GOLD moves by logical grid slices, not arbitrary floating
	// world distance.
	//
	// 64 cells:
	//     offset range = -32 ... +32
	// ---------------------------------------------------------
	const int halfSlice = kIdleGridDim / 2;

	const int sliceOffset =
		static_cast<int>(round(-halfSlice +
			local * static_cast<float>(halfSlice * 2)));

	const int sliceIndex =
		std::clamp(halfSlice + sliceOffset, 0, kIdleGridDim);

	EuclidRenderer::GridPlane plane =
		EuclidRenderer::GridPlane::PLANE_XY;

	float planePosition = 0.0f;

	switch (segment) {
	case 0:
		// XY plane moves through Z
		plane = EuclidRenderer::GridPlane::PLANE_XY;

		planePosition = grid.origin.z +
			static_cast<float>(sliceIndex) * grid.cellSize.z;

		break;

	case 1:
		// XZ plane moves through Y
		plane = EuclidRenderer::GridPlane::PLANE_XZ;

		planePosition = grid.origin.y +
			static_cast<float>(sliceIndex) * grid.cellSize.y;

		break;

	default:
		// YZ plane moves through X
		plane = EuclidRenderer::GridPlane::PLANE_YZ;

		planePosition = grid.origin.x +
			static_cast<float>(sliceIndex) * grid.cellSize.x;

		break;
	}

	renderer.drawGridPlane(
		grid, 
		plane, 
		planePosition, 
		false
	);
}

bool DiagnosticIdle::handleInput(
	const WorkspaceInputEvent& input,
	WorkspaceServices& services) {

	return false;
}
