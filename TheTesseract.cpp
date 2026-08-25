#include "kernel.h"
#include "TheTesseract.h"

#include <GL/freeglut.h>
#include <algorithm>
#include <cstdio>
#include <cmath>

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
	synchronizeActiveCartridge();
	if (!m_activeWorkspace)
		return;

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
	synchronizeActiveCartridge();
	if (!m_activeWorkspace)
		return;

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
	if (!m_activeWorkspace)
		return false;


	// ---------------------------------------------------------
	// Route generic input event to active cartridge.
	// ---------------------------------------------------------
	const bool handled =
		m_activeWorkspace->handleInput(event, m_services);

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

bool Tesseract::singleParticleHasCommittedGeometry() const {
	return m_singleParticleWorkspace.hasCommittedGeometry();
}

float Tesseract::singleParticleRadius() const {
	return m_singleParticleWorkspace.particleRadius();
}

void Tesseract::activateLoadedSingleParticleBase(bool editableVolumeRestored) {
	m_singleParticleWorkspace.activateLoadedStaticParticleBase(editableVolumeRestored);
}

void Tesseract::synchronizeActiveCartridge() {
	if (!m_services.arbiter) return;

	IWorkspace* desired = &m_diagnosticIdle;
	const char* desiredName = "DIAGNOSTIC_IDLE";

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

