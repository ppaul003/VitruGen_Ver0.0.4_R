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
	(void)services;
}

void DiagnosticIdle::exit(WorkspaceServices& services) {
}

void DiagnosticIdle::update(
	const WorkspaceFrameContext& frame,
	WorkspaceServices& services) {
	
	const float dt = frame.deltaTime;
	switch (m_visualTransition) {

	// -----------------------------------------------------
	// NORMAL LAYER 0
	// -----------------------------------------------------
	case VisualTransitionState::Idle:
		
		m_previewRotationDegrees +=
			kPreviewRotationSpeed * dt;

		m_sliceTravel += kSliceCycleSpeed * dt;

		break;

	// -----------------------------------------------------
	// GRID_3D ENTER:
	// keep rotating until the NEXT +Z/front pass.
	// -----------------------------------------------------
	case VisualTransitionState::Grid3D_OrientToFront:

		m_previewRotationDegrees +=
			kPreviewRotationSpeed * dt;

		// Slice animation continues normally while
		// orientation waits for its next pass.
		m_sliceTravel += kSliceCycleSpeed * dt;
		if (m_previewRotationDegrees >= m_targetRotationDegrees) {

			m_previewRotationDegrees = m_targetRotationDegrees;

			// Find the NEXT XY center crossing:
			//
			// XY sweep occupies cycle 0 -> 1.
			// center occurs at 0.5.
			//
			const float cycle = floor(m_sliceTravel / 3.0f);
			float target = cycle * 3.0f + 0.5f;

			if (target <= m_sliceTravel) {

				target += 3.0f;
			}

			m_targetSliceTravel = target;

			m_visualTransition =
				VisualTransitionState::
				Grid3D_CaptureSlice;
		}

		break;

	// -----------------------------------------------------
	// Wait for XY plane to reach Z = 0.
	// -----------------------------------------------------
	case VisualTransitionState::Grid3D_CaptureSlice:

		m_sliceTravel += kSliceCycleSpeed * dt;
		if (m_sliceTravel >= m_targetSliceTravel) {

			m_sliceTravel = m_targetSliceTravel;

			m_visualTransition =
				VisualTransitionState::
				Grid3D_HoldCenter;

			m_grid3DEnterComplete = true;
		}

		break;

	// -----------------------------------------------------
	// Hold cube front-facing with XY slice at Z=0
	// while Camera performs its transition.
	// -----------------------------------------------------
	case VisualTransitionState::Grid3D_HoldCenter:

		break;

	// -----------------------------------------------------
	// RETURN:
	// release XY slice from center through remainder
	// of its Z traversal.
	// -----------------------------------------------------
	case VisualTransitionState::Grid3D_ReleaseSlice:

		m_sliceTravel += kSliceCycleSpeed * dt;
		if (m_sliceTravel >= m_targetSliceTravel) {

			m_sliceTravel = m_targetSliceTravel;
			m_visualTransition = VisualTransitionState::Idle;
			m_grid3DReturnComplete = true;
		}

		break;
	}
}

void DiagnosticIdle::render(
	const WorkspaceFrameContext& frame,
	WorkspaceServices& services) {

	if (!services.renderer) return;

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

	glPushMatrix();

	const float visualRotation =
		fmod(m_previewRotationDegrees, 360.0f);

	glRotatef(visualRotation, 0.0f, 1.0f, 0.0f);

	// UniformGrid
	renderer.drawUniformGrid(grid, display);

	float sliceCycle = fmod(m_sliceTravel, 3.0f);
	if (sliceCycle < 0.0f) sliceCycle += 3.0f;

	const int segment =
		std::min(2, static_cast<int>(sliceCycle));

	const float local = sliceCycle -
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

	// slice
	renderer.drawGridPlane(grid, plane, planePosition, false);
	glPopMatrix();
}

bool DiagnosticIdle::handleInput(
	const WorkspaceInputEvent& input,
	WorkspaceServices& services) {

	if (!m_arbiter) m_arbiter = services.arbiter;
	if (!m_arbiter) return false;

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
			if (m_arbiter->getWorkspaceDomain() != TheArbiter::WorkspaceDomain::NONE) {
				m_arbiter->requestEnterDomain(m_arbiter->getWorkspaceDomain());
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

		m_arbiter->requestReturnToGlobalShell(m_arbiter->getWorkspaceDomain());
		return true;
	}

	return false;
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
	environmentRow.label = "[1]: ENVIRONMENT SELECTION";
	environmentRow.value = selectedEnvironmentName();

	environmentRow.selectable = true;
	environmentRow.selected = true;

	environmentSection.rows.push_back(environmentRow);
	presentation.sections.push_back(environmentSection);

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

void DiagnosticIdle::beginGrid3DEnterTransition() {

	m_grid3DEnterComplete = false;
	m_grid3DReturnComplete = false;

	const float revolution =
		floor(m_previewRotationDegrees / 360.0f);

	// Strictly NEXT pass
	m_targetRotationDegrees =
		(revolution + 1.0f) * 360.0f;

	m_visualTransition =
		VisualTransitionState::Grid3D_OrientToFront;

}

void DiagnosticIdle::beginGrid3DReturnTransition() {

	m_grid3DReturnComplete = false;
	// Cube remains exactly front-facing.
	//
	// m_sliceTravel was deliberately frozen at
	// an XY center crossing during entry.
	//
	// Continue from center to end of that XY pass.
	m_targetSliceTravel =
		m_sliceTravel + 0.5f;

	m_visualTransition =
		VisualTransitionState::
		Grid3D_ReleaseSlice;
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

	for (int index = 0; index < 4; index++) {

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


