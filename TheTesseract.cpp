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
	// DiagnosticIdle is the first cartridge inserted into the
	// Tesseract workspace socket.
	//
	// Future workspace selection / registry logic will replace
	// this direct assignment.
	// ---------------------------------------------------------
	m_activeWorkspace = &m_diagnosticIdle;

	// ---------------------------------------------------------
	// Initialize cartridge.
	// ---------------------------------------------------------
	if (!m_activeWorkspace->initialize(m_services)) {

		printf(
			"[Tesseract] ERROR: "
			"DiagnosticIdle initialization failed.\n"
		);

		m_activeWorkspace = nullptr;

		return false;
	}

	// ---------------------------------------------------------
	// Enter cartridge runtime.
	// ---------------------------------------------------------
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

void Tesseract::update(const WorkspaceFrameContext& frame) {
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
	return m_activeWorkspace->handleInput(event, m_services);
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
