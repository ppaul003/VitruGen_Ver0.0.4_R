#include "DiagnosticIdle.h"
#include "renderer_Euclid.h"

using namespace glm;

bool DiagnosticIdle::initialize(
	WorkspaceServices& services) {

	return services.renderer != nullptr;
}

void DiagnosticIdle::enter(
	WorkspaceServices& services) {

	m_rotation = 0.0f;

	m_sliceXY = -1.0f;
	m_sliceXZ = -1.0f;
	m_sliceYZ = -1.0f;
}

void DiagnosticIdle::exit(
	WorkspaceServices& services) {
}

void DiagnosticIdle::update(
	const WorkspaceFrameContext& frame,
	WorkspaceServices& services) {

	constexpr float sliceSpeed = 0.50f;

	m_rotation +=
		30.0f * frame.deltaTime;

	m_sliceXY += sliceSpeed * frame.deltaTime;
	m_sliceXZ += sliceSpeed * frame.deltaTime;
	m_sliceYZ += sliceSpeed * frame.deltaTime;

	// Temporary diagnostic bounds.
	// We can derive these from the grid later.
	if (m_sliceXY > 2.0f)
		m_sliceXY = -2.0f;

	if (m_sliceXZ > 2.0f)
		m_sliceXZ = -2.0f;

	if (m_sliceYZ > 2.0f)
		m_sliceYZ = -2.0f;
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
	float gridDim = renderer.getGridDimSize();

	grid.dimensions =
		ivec3(gridDim, gridDim, gridDim);

	float halfBox =
		renderer.getSimBoxSize() * 0.5f;

	grid.origin = vec3(-halfBox);

	float cellSize =
		renderer.getSimBoxSize() /
		static_cast<float>(gridDim);

	grid.cellSize = vec3(cellSize);

	grid.majorEvery =
		renderer.getGridMajorEvery();

	EuclidRenderer::GridDisplay display;
	display.boundary = true;
	display.majorGrid = true;
	display.minorGrid = false;
	display.axes = true;

	renderer.drawUniformGrid(grid, display);

	// Animated diagnostic slices
	renderer.drawGridPlane(
		grid,
		EuclidRenderer::GridPlane::PLANE_XY,
		m_sliceXY,
		true
	);

	renderer.drawGridPlane(
		grid,
		EuclidRenderer::GridPlane::PLANE_XZ,
		m_sliceXZ,
		true
	);

	renderer.drawGridPlane(
		grid,
		EuclidRenderer::GridPlane::PLANE_YZ,
		m_sliceYZ,
		true
	);
}

bool DiagnosticIdle::handleInput(
	const WorkspaceInputEvent& input,
	WorkspaceServices& services) {

	return false;
}
