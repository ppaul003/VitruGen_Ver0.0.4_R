#include "kernel.h"
#include "TheTesseract.h"

#include <GL/freeglut.h>
#include <algorithm>
#include <cstdio>
#include <cmath>

#include "Camera.h"

using namespace std;

namespace {
	float smoothStep01(float t) {
		if (t < 0.0f) return 0.0f;
		if (t > 1.0f) return 1.0f;

		return t * t * (3.0f - 2.0f * t);
	}

	float lerp(float a, float b, float t) {
		return a + (b - a) * t;
	}
}

bool Tesseract::initialize(WorkspaceServices services) {
	// ---------------------------------------------------------
	// Store the host services supplied by EuclidEngine.
	//
	// These are non-owning references to generic host systems.
	// The active workspace receives them through the workspace
	// socket rather than reaching directly into EuclidEngine.
	// ---------------------------------------------------------
	m_services = services;

	// ---------------------------------------------------------
	// Initialize cartridge.
	// ---------------------------------------------------------
	if (!m_diagnosticIdle.initialize(m_services)) {

		printf(
			"[Tesseract] ERROR: "
			"DiagnosticIdle initialization failed.\n"
		);

		return false;
	}

	if (!m_grid2DWorkspace.initialize(m_services)) {
		printf(
			"[Tesseract] ERROR: "
			"Grid2DWorkspace initialization failed.\n");
		return false;
	}

	if (!m_singleParticleWorkspace.initialize(m_services)) {
		printf(
			"[Tesseract] ERROR: "
			"SingleParticleWorkspace initialization failed.\n");
		return false;
	}
	// ---------------------------------------------------------
	// DiagnosticIdle is the first cartridge inserted into the
	// Tesseract workspace socket.
	//
	// Future workspace selection / registry logic will replace
	// this direct assignment.
	// ---------------------------------------------------------
	m_activeWorkspace = &m_diagnosticIdle;
	m_activeWorkspace->enter(m_services);

	printf(
		"[Tesseract] Workspace socket initialized.\n"
	);

	printf(
		"[Tesseract] Active workspace: "
		"DIAGNOSTIC_IDLE\n"
	);

	return true;
}

void Tesseract::shutdown() {
	if (m_activeWorkspace) m_activeWorkspace->exit(m_services);
	m_activeWorkspace = nullptr;
	
	m_singleParticleWorkspace.shutdown();
}

void Tesseract::update(const WorkspaceFrameContext& frame) {
	processNavigationRequest();

	if (domainTransitionActive()) {
		updateDomainTransition(frame);
		return;
	}

	synchronizeActiveCartridge();
	if (!m_activeWorkspace) return;

	// ---------------------------------------------------------
	// Tesseract does not interpret workspace update behavior.
	//
	// It simply routes the frame context through the socket.
	// ---------------------------------------------------------
	m_activeWorkspace->update(frame, m_services);
}

// =============================================================================
// RENDER
// =============================================================================
void Tesseract::render(const WorkspaceFrameContext& frame) {

	// ---------------------------------------------------------
	// Transition-specific visual routing.
	// ---------------------------------------------------------
	if (domainTransitionActive()) {

		switch (m_domainTransitionPhase) {

		case DomainTransitionPhase::ENTER_DOMAIN_VISUAL:
		case DomainTransitionPhase::ENTER_DOMAIN_READY:
		case DomainTransitionPhase::ENTER_CAMERA:
			m_diagnosticIdle.render(frame, m_services);
			return;

		case DomainTransitionPhase::ENTER_WORKSPACE_CAMERA:
		case DomainTransitionPhase::EXIT_WORKSPACE_CAMERA:
			m_grid2DWorkspace.render(frame, m_services);
			return;

		case DomainTransitionPhase::EXIT_CAMERA:

			if (m_activeWorkspace) {
				m_activeWorkspace->render(frame, m_services);
			}
			return;

		case DomainTransitionPhase::EXIT_DOMAIN_VISUAL:

			m_diagnosticIdle.render(frame,m_services);
			return;


		case DomainTransitionPhase::NONE:
		default:
			break;
		}
	}

	synchronizeActiveCartridge();
	if (!m_activeWorkspace) return;

	// ---------------------------------------------------------
	// Workspace decides WHAT should be rendered.
	//
	// EuclidRenderer, supplied through WorkspaceServices,
	// decides HOW those generic components are rendered.
	// ---------------------------------------------------------
	m_activeWorkspace->render(frame, m_services);
}

// =============================================================================
// INPUT
// =============================================================================
bool Tesseract::handleInput(const WorkspaceInputEvent& event) {
	if (workspaceInputLocked()) return true;
	if (!m_activeWorkspace) return false;

	// ---------------------------------------------------------
	// Route generic input event to active cartridge.
	// ---------------------------------------------------------
	const bool handled =
		m_activeWorkspace->handleInput(event, m_services);

	// E/Q may have generated an Arbiter navigation request.
	processNavigationRequest();

	if (!domainTransitionActive())
		synchronizeActiveCartridge();

	return handled;
}

// =============================================================================
// PRESENTATION
// =============================================================================
WorkspacePresentation 
Tesseract::presentation() const {
	if (!m_activeWorkspace) {

		WorkspacePresentation presentation;

		presentation.panelVisible = true;
		presentation.workspaceName = "VITRUGEN HOST";
		presentation.layerLabel = "NO ACTIVE WORKSPACE";
		presentation.statusLine = "WORKSPACE SOCKET OFFLINE";

		return presentation;
	}

	// =========================================================
	// GRID_2D DOMAIN / WORKSPACE TRANSITION PRESENTATION
	// =========================================================
	if (m_transitionDomain == TheArbiter::WorkspaceDomain::GRID_2D) {
		switch (m_domainTransitionPhase) {
		case DomainTransitionPhase::ENTER_DOMAIN_VISUAL: {
			WorkspacePresentation p = m_diagnosticIdle.buildPresentation();
			p.statusLine = "AUTO: Transitioning To GRID_2D...";
			p.statusTone = WorkspaceStatusTone::Transition;
			p.statusBlink = true;
			p.frameTone = WorkspaceStatusTone::Transition;
			p.frameBlink = true;
			return p;
		}
		case DomainTransitionPhase::ENTER_DOMAIN_READY: {
			WorkspacePresentation p = m_diagnosticIdle.buildPresentation();
			p.statusLine = "READY: GRID_2D Setup Complete.";
			p.statusTone = WorkspaceStatusTone::Ready;
			p.frameTone = WorkspaceStatusTone::Ready;
			return p;
		}
		case DomainTransitionPhase::ENTER_CAMERA:
			return m_grid2DWorkspace.buildLayer1TransitionPresentation();

		case DomainTransitionPhase::ENTER_WORKSPACE_CAMERA: {
			WorkspacePresentation p = m_grid2DWorkspace.buildPresentation();
			p.statusLine = "AUTO: Transitioning To TEXTURE_MAP_2D TARGET CONFIGURATION...";
			p.statusTone = WorkspaceStatusTone::Transition;
			p.statusBlink = true;
			p.frameTone = WorkspaceStatusTone::Transition;
			p.frameBlink = true;
			return p;
		}
		case DomainTransitionPhase::EXIT_WORKSPACE_CAMERA:
			return m_grid2DWorkspace.buildPresentation();

		default:
			break;
		}
	}

	// =========================================================
	// GRID_3D FORWARD TRANSITION PRESENTATION
	// =========================================================
	if (m_transitionDomain == TheArbiter::WorkspaceDomain::GRID_3D) {
		
		switch (m_domainTransitionPhase) {
		// -----------------------------------------------------
		// Tesseract is still moving.
		// Keep Layer-0 panel, replace only its status.
		// -----------------------------------------------------
		case DomainTransitionPhase::ENTER_DOMAIN_VISUAL: {

			WorkspacePresentation presentation =
				m_diagnosticIdle.buildPresentation();

			presentation.statusLine =
				"AUTO: Transitioning To GRID_3D...";

			presentation.statusTone =
				WorkspaceStatusTone::Transition;

			presentation.statusBlink = true;

			presentation.frameTone =
				WorkspaceStatusTone::Transition;

			presentation.frameBlink = true;

			return presentation;
		}

		// -----------------------------------------------------
		// Tesseract is captured and ready.
		// Briefly acknowledge completion.
		// -----------------------------------------------------
		case DomainTransitionPhase::ENTER_DOMAIN_READY: {

			WorkspacePresentation presentation =
				m_diagnosticIdle.buildPresentation();

			presentation.statusLine =
				"READY: GRID_3D Setup Complete.";

			presentation.statusTone =
				WorkspaceStatusTone::Ready;

			presentation.statusBlink = false;

			presentation.frameTone =
				WorkspaceStatusTone::Ready;

			presentation.frameBlink = false;
			
			return presentation;
		}

		case DomainTransitionPhase::ENTER_CAMERA: {
			WorkspacePresentation presentation =
				m_singleParticleWorkspace.buildLayer1TransitionPresentation();

			presentation.frameTone = WorkspaceStatusTone::Ready;
			presentation.frameBlink = false;

			return presentation;
		}

		case DomainTransitionPhase::NONE:
		default:
			break;
		}
	}

	// ---------------------------------------------------------
	// Workspace supplies semantic UI data.
	//
	// Tesseract does not format or interpret it.
	// ViewPort will handle its visual presentation.
	// ---------------------------------------------------------
	return m_activeWorkspace->buildPresentation();
}

WorkspaceMenuPresentation Tesseract::menu() const {
	return m_activeWorkspace
		? m_activeWorkspace->buildMenu()
		: WorkspaceMenuPresentation{};
}

bool Tesseract::handleMenuCommand(int command) {
	if (!m_activeWorkspace) return false;
	const bool handled = m_activeWorkspace->handleMenuCommand(command, m_services);
	synchronizeActiveCartridge();
	return handled;
}

SingleParticleWorkspace::HostRequest
Tesseract::takeSingleParticleHostRequest() {
	return m_singleParticleWorkspace.takeHostRequest();
}

Grid2DWorkspace::HostRequest Tesseract::takeGrid2DHostRequest() {
	return m_grid2DWorkspace.takeHostRequest();
}

void Tesseract::replaceGrid2DOutputCatalog(
	std::vector<vitru::StaticAssetCatalogEntry> catalog) {
	m_grid2DWorkspace.replaceOutputCatalog(std::move(catalog));
}

void Tesseract::completeGrid2DTargetLoad(
	bool loaded,
	bool ready,
	vitru::AssetId assetId,
	const std::string& displayName,
	const std::string& message) {
	m_grid2DWorkspace.completeTargetLoad(
		loaded, ready, assetId, displayName, message);
}

bool Tesseract::exportSingleParticleVolume(std::vector<float>& output) const {
	return m_singleParticleWorkspace.exportWorkingVolumeToHost(output);
}

bool Tesseract::restoreSingleParticleVolume(
	const std::vector<float>& input,
	const int3& size) {
	return m_singleParticleWorkspace.restoreCommittedVolumeFromHost(input, size);
}

const int3& Tesseract::singleParticleVolumeSize() const {
	return m_singleParticleWorkspace.getVolumeSize();
}

MarchingCubes* Tesseract::singleParticleMarchingCubes() const {
	return m_singleParticleWorkspace.marchingCubes();
}

bool Tesseract::prepareSingleParticleMarchingCubesExport() {
	return m_singleParticleWorkspace.prepareMarchingCubesExport();
}

void Tesseract::activateSingleParticleExportedMeshRender() {
	m_singleParticleWorkspace.activateExportedMeshRender();
}

bool Tesseract::singleParticleHasCommittedGeometry() const {
	return m_singleParticleWorkspace.hasCommittedGeometry();
}

float Tesseract::singleParticleRadius() const {
	return m_singleParticleWorkspace.particleRadius();
}

void Tesseract::activateLoadedSingleParticleBase(bool editableVolumeRestored) {
	m_singleParticleWorkspace.activateLoadedStaticParticleBase(editableVolumeRestored);
}

void Tesseract::processNavigationRequest() {
	if (!m_services.arbiter) return;

	// Do not accept another structural navigation request
	// while one transition is already being performed.
	if (domainTransitionActive()) return;
	if (!m_services.arbiter->hasNavigationRequest())
		return;

	const TheArbiter::NavigationRequest request =
		m_services.arbiter->takeNavigationRequest();

	using RequestType = TheArbiter::NavigationRequestType;
	using Domain = TheArbiter::WorkspaceDomain;
	using Layer = TheArbiter::ApplicationLayer;
	using Workspace = TheArbiter::WorkspaceId;

	switch (request.type) {

		// =========================================================
		// LAYER 0 -> DOMAIN
		// =========================================================
	case RequestType::ENTER_DOMAIN:

		m_transitionDomain = request.domain;

		// -----------------------------------------------------
		// GRID_3D gets the animated transition.
		//
		// IMPORTANT:
		// Do NOT change ApplicationLayer yet.
		// DiagnosticIdle remains inserted until both:
		//
		//     Tesseract visual transition
		//     camera transition
		//
		// have completed.
		// -----------------------------------------------------
		if (request.domain == Domain::GRID_3D ||
			request.domain == Domain::GRID_2D) {

			m_domainTransitionPhase =
				DomainTransitionPhase::ENTER_DOMAIN_VISUAL;

			if (request.domain == Domain::GRID_2D)
				m_diagnosticIdle.beginGrid2DEnterTransition();
			else
				m_diagnosticIdle.beginGrid3DEnterTransition();
			return;
		}

		// -----------------------------------------------------
		// SIMCAD_4D:
		// retain immediate behavior until their domain-specific
		// animations are implemented.
		// -----------------------------------------------------
		m_services.arbiter->setApplicationLayer(Layer::DOMAIN_SELECTION);
		synchronizeActiveCartridge();
		return;


		// =========================================================
		// DOMAIN -> LAYER 0
		// =========================================================
	case RequestType::RETURN_GLOBAL_SHELL:

		m_transitionDomain = request.domain;

		// -----------------------------------------------------
		// GRID_3D reverse ordering:
		//
		//     camera first
		//     Tesseract second
		// -----------------------------------------------------
		if (request.domain == Domain::GRID_3D ||
			request.domain == Domain::GRID_2D) {

			m_domainTransitionPhase =
				DomainTransitionPhase::EXIT_CAMERA;

			if (m_services.camera) {
				m_services.camera->beginTransitionToMenu(kGrid3DCamTransDuration);
			}
			else {
				// No camera service:
				// skip directly to the visual phase.
				m_domainTransitionPhase =
					DomainTransitionPhase::EXIT_DOMAIN_VISUAL;

				if (request.domain == Domain::GRID_2D)
					m_diagnosticIdle.beginGrid2DReturnTransition();
				else
					m_diagnosticIdle.beginGrid3DReturnTransition();
			}

			return;
		}

		// Other domains remain immediate for now.
		m_services.arbiter->setApplicationLayer(Layer::GLOBAL_SHELL);
		m_services.arbiter->setActiveWorkspace(Workspace::DIAGNOSTIC);
		m_transitionDomain = Domain::NONE;

		synchronizeActiveCartridge();
		return;

		// =========================================================
		// GRID_2D LAYER 1 -> TEXTURE_MAP_2D LAYER 2
		// =========================================================
	case RequestType::ENTER_WORKSPACE_CONFIGURATION:
		if (request.domain != Domain::GRID_2D ||
			request.workspace != Workspace::TEXTURE_MAP_2D) return;

		m_transitionDomain = request.domain;
		m_domainTransitionPhase =
			DomainTransitionPhase::ENTER_WORKSPACE_CAMERA;
		if (m_services.camera)
			m_services.camera->beginTransitionToStandardObject(
				kGrid3DCamTransDuration);
		return;

		// =========================================================
		// GRID_2D LAYER 2 -> LAYER 1
		// =========================================================
	case RequestType::RETURN_DOMAIN_SELECTION:
		if (request.domain != Domain::GRID_2D) return;

		m_transitionDomain = request.domain;
		m_domainTransitionPhase =
			DomainTransitionPhase::EXIT_WORKSPACE_CAMERA;
		if (m_services.camera)
			m_services.camera->beginTransitionToStandard2D(
				kGrid3DCamTransDuration);
		return;


	case RequestType::NONE:
	default:
		return;
	}
}

void Tesseract::updateDomainTransition(const WorkspaceFrameContext& frame) {
	if (!m_services.arbiter) return;
	if (!domainTransitionActive()) return;

	using Domain = TheArbiter::WorkspaceDomain;
	using Layer = TheArbiter::ApplicationLayer;
	using Workspace = TheArbiter::WorkspaceId;

	switch (m_domainTransitionPhase) {

		// =========================================================
		// ENTER GRID_3D — PHASE 1
		//
		// Cube:
		//     continue CCW
		//     wait for next +Z/front pass
		//     capture XY slice at Z = 0
		// =========================================================
	case DomainTransitionPhase::ENTER_DOMAIN_VISUAL:

		// DiagnosticIdle owns the actual cube/slice motion.
		m_diagnosticIdle.update(frame, m_services);
		if (m_transitionDomain == Domain::GRID_2D) {
			if (!m_diagnosticIdle.grid2DEnterVisualComplete()) return;
		}
		else if (!m_diagnosticIdle.grid3DEnterVisualComplete()) return;
		
		// -----------------------------------------------------
		// Tesseract is now:
		//
		//     front-facing
		//     XY plane at Z = 0
		//
		// Freeze that visual while the camera moves.
		// -----------------------------------------------------
		m_domainTransitionPhase =
			DomainTransitionPhase::ENTER_DOMAIN_READY;

		m_transitionPhaseElapsed = 0.0f;
		
		return;

	// =========================================================
	// ENTER GRID_3D — READY HOLD
	//
	// Tesseract:
	//     front facing
	//     XY slice captured at Z = 0
	//
	// Presentation:
	//     READY: GRID_3D Setup Complete.
	//
	// Camera:
	//     still at Layer-0 viewing pose
	// =========================================================
	case DomainTransitionPhase::ENTER_DOMAIN_READY:

		// DiagnosticIdle is in Grid3D_HoldCenter,
		// so this preserves the captured visual.
		m_diagnosticIdle.update(frame, m_services);
		m_transitionPhaseElapsed += frame.deltaTime;

		if (m_transitionPhaseElapsed < kGrid3DReadyHoldDuration)
			return;

		// -----------------------------------------------------
		// READY acknowledgement complete.
		// Begin camera movement into Layer 1.
		// -----------------------------------------------------
		m_domainTransitionPhase =
			DomainTransitionPhase::ENTER_CAMERA;

		m_transitionPhaseElapsed = 0.0f;
		
		if (m_services.camera) {
			if (m_transitionDomain == Domain::GRID_2D)
				m_services.camera->beginTransitionToStandard2D(kGrid3DCamTransDuration);
			else
				m_services.camera->beginTransitionToStandard3D(kGrid3DCamTransDuration);
		}
		

		return;

	// =========================================================
	// ENTER GRID_3D — PHASE 2
	//
	// Camera moves toward centered GRID_3D view.
	// Cube remains frozen front-facing.
	// =========================================================
	case DomainTransitionPhase::ENTER_CAMERA:

		// DiagnosticIdle should now be in its hold-center
		// state, so this update preserves the frozen visual.
		m_diagnosticIdle.update(frame, m_services);

		if (m_services.camera) {

			m_services.camera->updatePoseTransition(frame.deltaTime);
			if (m_services.camera->poseTransitionActive()) return;
		}

		// -----------------------------------------------------
		// BOTH transitions are complete.
		//
		// Only NOW commit Layer 1.
		// -----------------------------------------------------
		m_services.arbiter->setApplicationLayer(Layer::DOMAIN_SELECTION);
		if (m_transitionDomain == Domain::GRID_2D) {
			m_services.arbiter->setActiveWorkspace(
				m_grid2DWorkspace.selectedWorkspaceId());
			if (m_services.camera)
				m_services.camera->setBehaviorMode(
					CameraProcessor::CAM_STANDARD_2D_LOCKED);
		}
		else if (m_services.camera) {
			m_services.camera->setBehaviorMode(
				CameraProcessor::CAM_STANDARD_3D_ORBIT);
		}
		m_domainTransitionPhase = DomainTransitionPhase::NONE;
		m_transitionDomain = Domain::NONE;

		// This will replace DiagnosticIdle with the currently
		// configured GRID_3D cartridge.
		synchronizeActiveCartridge();
		return;

		// =========================================================
		// RETURN TO LAYER 0 — PHASE 1
		//
		// Camera moves back first.
		// SINGLE_PARTICLE remains structurally active.
		// =========================================================
	case DomainTransitionPhase::EXIT_CAMERA:

		// Keep the currently active workspace alive while its
		// camera retreats.
		if (m_activeWorkspace && m_activeWorkspace != &m_diagnosticIdle) {
			m_activeWorkspace->update(frame, m_services);
		}

		if (m_services.camera) {
			m_services.camera->updatePoseTransition(frame.deltaTime);
			if (m_services.camera->poseTransitionActive()) return;
		}

		// -----------------------------------------------------
		// Camera has reached Layer-0 pose.
		//
		// Start the Tesseract reappearance:
		// XY plane starts at Z = 0 and continues its sweep.
		// -----------------------------------------------------
		m_domainTransitionPhase =
			DomainTransitionPhase::EXIT_DOMAIN_VISUAL;

		if (m_transitionDomain == Domain::GRID_2D)
			m_diagnosticIdle.beginGrid2DReturnTransition();
		else
			m_diagnosticIdle.beginGrid3DReturnTransition();
		return;

		// =========================================================
		// RETURN TO LAYER 0 — PHASE 2
		//
		// XY slice releases from center.
		// Cube resumes normal counter-clockwise idle rotation.
		// =========================================================
	case DomainTransitionPhase::EXIT_DOMAIN_VISUAL:

		m_diagnosticIdle.update(frame, m_services);
		if (m_transitionDomain == Domain::GRID_2D) {
			if (!m_diagnosticIdle.grid2DReturnVisualComplete()) return;
		}
		else if (!m_diagnosticIdle.grid3DReturnVisualComplete()) return;

		// -----------------------------------------------------
		// Reverse transition complete.
		//
		// NOW commit Layer 0.
		// -----------------------------------------------------
		m_services.arbiter->setApplicationLayer(Layer::GLOBAL_SHELL);
		m_services.arbiter->setActiveWorkspace(Workspace::DIAGNOSTIC);

		if (m_services.camera) {
			m_services.camera->setBehaviorMode(CameraProcessor::CAM_MENU_PREVIEW);
		}

		m_domainTransitionPhase = DomainTransitionPhase::NONE;
		m_transitionDomain = Domain::NONE;
		synchronizeActiveCartridge();
		return;

	// =========================================================
	// GRID_2D LAYER 1 -> LAYER 2 CAMERA
	// =========================================================
	case DomainTransitionPhase::ENTER_WORKSPACE_CAMERA:
		m_grid2DWorkspace.update(frame, m_services);
		if (m_services.camera) {
			m_services.camera->updatePoseTransition(frame.deltaTime);
			if (m_services.camera->poseTransitionActive()) return;
			m_services.camera->setBehaviorMode(
				CameraProcessor::CAM_STANDARD_OBJECT_ORBIT);
		}
		m_services.arbiter->setWorkspaceDomain(Domain::GRID_2D);
		m_services.arbiter->setActiveWorkspace(Workspace::TEXTURE_MAP_2D);
		m_services.arbiter->setApplicationLayer(Layer::WORKSPACE_CONFIGURATION);
		m_domainTransitionPhase = DomainTransitionPhase::NONE;
		m_transitionDomain = Domain::NONE;
		synchronizeActiveCartridge();
		return;

	// =========================================================
	// GRID_2D LAYER 2 -> LAYER 1 CAMERA
	// =========================================================
	case DomainTransitionPhase::EXIT_WORKSPACE_CAMERA:
		m_grid2DWorkspace.update(frame, m_services);
		if (m_services.camera) {
			m_services.camera->updatePoseTransition(frame.deltaTime);
			if (m_services.camera->poseTransitionActive()) return;
			m_services.camera->setBehaviorMode(
				CameraProcessor::CAM_STANDARD_2D_LOCKED);
		}
		m_services.arbiter->setWorkspaceDomain(Domain::GRID_2D);
		m_services.arbiter->setActiveWorkspace(Workspace::TEXTURE_MAP_2D);
		m_services.arbiter->setApplicationLayer(Layer::DOMAIN_SELECTION);
		m_domainTransitionPhase = DomainTransitionPhase::NONE;
		m_transitionDomain = Domain::NONE;
		synchronizeActiveCartridge();
		return;

	case DomainTransitionPhase::NONE:
	default:
		return;
	}
}

void Tesseract::synchronizeActiveCartridge() {
	if (!m_services.arbiter) return;

	IWorkspace* desired = &m_diagnosticIdle;
	const char* desiredName = "DIAGNOSTIC_IDLE";

	if (!m_services.arbiter->isGlobalShell() &&
		m_services.arbiter->getWorkspaceDomain() ==
		TheArbiter::WorkspaceDomain::GRID_2D) {

		desired = &m_grid2DWorkspace;
		desiredName = "GRID_2D_SELECTOR";
	}

	if (!m_services.arbiter->isGlobalShell() &&
		m_services.arbiter->getWorkspaceDomain() ==
		TheArbiter::WorkspaceDomain::GRID_3D) {

		desired = &m_singleParticleWorkspace;
		desiredName = "SINGLE_PARTICLE_MCAD";
	}

	activateCartridge(desired, desiredName);
}

void Tesseract::activateCartridge(
	IWorkspace* workspace,
	const char* name) {

	if (!workspace || workspace == m_activeWorkspace) return;
	if (m_activeWorkspace) m_activeWorkspace->exit(m_services);
	m_activeWorkspace = workspace;
	m_activeWorkspace->enter(m_services);

	printf("[Tesseract] Active workspace: %s\n", name);
}

