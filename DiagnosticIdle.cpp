#include "DiagnosticIdle.h"
#include "renderer_Euclid.h"
#include "TheArbiter.h"

#include <cmath>
#include <algorithm>

using namespace std;
using namespace glm;

bool DiagnosticIdle::initialize(
	WorkspaceServices& services) {
	
	m_arbiter = services.arbiter;
	
	return
		services.renderer != nullptr &&
		m_arbiter != nullptr;
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

	// ---------------------------------------------------------
	// DiagnosticIdle remains inserted while Layer-1 cartridges
	// are intentionally outside this sprint.  The structural
	// transition is still recorded by TheArbiter, and this small
	// fallback makes that boundary explicit without implementing
	// any domain workspace behavior.
	// ---------------------------------------------------------
	if (m_arbiter && !m_arbiter->isGlobalShell()) {

		presentation.workspaceName =
			std::string("LAYER 1 -> ") +
			selectedEnvironmentName() +
			" WORKSPACE CONFIGURATION";

		presentation.statusLine =
			std::string(selectedEnvironmentName()) +
			" cartridge is not migrated.";

		presentation.statusTone =
			WorkspaceStatusTone::Warning;

		presentation.footerLine1 =
			"Q: Back one layer";

		presentation.footerLine2 =
			"ESC: Exit";

		return presentation;
	}

	presentation.workspaceName =
		"LAYER 0 -> MENU";

	WorkspacePanelSection environmentSection;

	WorkspacePanelRow environmentRow;
	environmentRow.label =
		"[1]: ENVIRONMENT SELECTION";
	environmentRow.value =
		selectedEnvironmentName();
	environmentRow.selectable = true;
	environmentRow.selected = true;

	environmentSection.rows.push_back(
		environmentRow
	);

	presentation.sections.push_back(
		environmentSection
	);

	if (!m_arbiter ||
		m_arbiter->getWorkspaceDomain() ==
		TheArbiter::WorkspaceDomain::NONE) {

		presentation.statusLine =
			"IDLE selected: E is locked.";

		presentation.statusTone =
			WorkspaceStatusTone::Warning;
	}
	else {

		presentation.statusLine =
			std::string(selectedEnvironmentName()) +
			" selected: press E to configure.";

		presentation.statusTone =
			WorkspaceStatusTone::Ready;
	}

	presentation.footerLine1 =
		"A / D: Change selection     E: Enter";

	presentation.footerLine2 =
		"ESC: Exit";

	return presentation;
}

const char* DiagnosticIdle::selectedEnvironmentName() const {

	if (!m_arbiter)
		return "IDLE";

	switch (m_arbiter->getWorkspaceDomain()) {
	case TheArbiter::WorkspaceDomain::NONE:
		return "IDLE";

	case TheArbiter::WorkspaceDomain::GRID_2D:
		return "GRID_2D";

	case TheArbiter::WorkspaceDomain::GRID_3D:
		return "GRID_3D";

	case TheArbiter::WorkspaceDomain::SIMCAD_4D:
		return "SIMCAD_4D";
	}

	return "IDLE";
}

void DiagnosticIdle::cycleEnvironment(int direction) {

	if (!m_arbiter || direction == 0)
		return;

	using Domain = TheArbiter::WorkspaceDomain;

	static constexpr Domain kEnvironmentOrder[] = {
		Domain::NONE,
		Domain::GRID_2D,
		Domain::GRID_3D,
		Domain::SIMCAD_4D
	};

	int currentIndex = 0;

	for (int index = 0; index < 4; ++index) {

		if (kEnvironmentOrder[index] ==
			m_arbiter->getWorkspaceDomain()) {

			currentIndex = index;
			break;
		}
	}

	const int step = direction < 0 ? -1 : 1;
	const int nextIndex =
		(currentIndex + step + 4) % 4;

	m_arbiter->setWorkspaceDomain(
		kEnvironmentOrder[nextIndex]
	);
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

	if (!m_arbiter)
		m_arbiter = services.arbiter;

	if (!m_arbiter)
		return false;

	// ---------------------------------------------------------
	// The root cartridge supplies Layer-0 meaning for the
	// generic actions translated by TheArbiter.
	// ---------------------------------------------------------
	if (m_arbiter->isGlobalShell()) {

		switch (input.action) {
		case WorkspaceInputAction::Decrease:
			cycleEnvironment(-1);
			return true;

		case WorkspaceInputAction::Increase:
			cycleEnvironment(+1);
			return true;

		case WorkspaceInputAction::Activate:
			// GOLD keeps E locked while IDLE is selected.
			if (m_arbiter->getWorkspaceDomain() !=
				TheArbiter::WorkspaceDomain::NONE) {

				m_arbiter->setApplicationLayer(
					TheArbiter::ApplicationLayer::DOMAIN_SELECTION
				);
			}

			return true;

		case WorkspaceInputAction::Back:
			// GOLD treats Back at the root as a handled no-op.
			return true;

		default:
			return false;
		}
	}

	// No domain cartridge is migrated yet.  Permit only the
	// structural return to Layer 0 while DiagnosticIdle remains
	// the active fallback cartridge.
	if (m_arbiter->isDomainSelection() &&
		input.action == WorkspaceInputAction::Back) {

		m_arbiter->setApplicationLayer(
			TheArbiter::ApplicationLayer::GLOBAL_SHELL
		);

		return true;
	}

	return false;
}
