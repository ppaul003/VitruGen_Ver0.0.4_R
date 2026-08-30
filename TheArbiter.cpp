#include "TheArbiter.h"

#include <GL/freeglut.h>
#include <algorithm>
#include <cassert>
#include <cmath>

// =============================================================================
// LIFECYCLE
// =============================================================================
TheArbiter::TheArbiter() {

	m_navigation.layer =
		ApplicationLayer::GLOBAL_SHELL;

	m_navigation.domain =
		WorkspaceDomain::NONE;

	m_navigation.workspace =
		WorkspaceId::DIAGNOSTIC;
}

TheArbiter::~TheArbiter() {
}

// =============================================================================
// KEYBOARD ROUTING
// =============================================================================
TheArbiter::ArbiterResult
TheArbiter::routeKeyboard(const KeyboardInput::KeyEvent& event) const {

	ArbiterResult result;

	result.workspaceInput.x = event.x;
	result.workspaceInput.y = event.y;

	// ---------------------------------------------------------
	// HOST-LEVEL INPUT
	// ---------------------------------------------------------
	switch (event.signal) {
	case KeyboardInput::KEY_ESCAPE:

		result.handled = true;

		result.arbiterCommand =
			ArbiterCommand::CMD_EXIT;

		return result;


	default:
		break;
	}

	// ---------------------------------------------------------
	// GENERIC WORKSPACE INPUT
	//
	// Arbiter translates physical keys into semantic actions.
	//
	// It does NOT decide whether the action is legal.
	//
	// The active workspace decides that.
	// ---------------------------------------------------------
	switch (event.signal) {

	case KeyboardInput::KEY_W:
		result.workspaceInput.action =
			WorkspaceInputAction::Previous;

		break;

	case KeyboardInput::KEY_S:
		result.workspaceInput.action =
			WorkspaceInputAction::Next;

		break;

	case KeyboardInput::KEY_A:
		result.workspaceInput.action =
			WorkspaceInputAction::Decrease;

		break;

	case KeyboardInput::KEY_D:
		result.workspaceInput.action =
			WorkspaceInputAction::Increase;

		break;

	case KeyboardInput::KEY_E:
	case KeyboardInput::KEY_ENTER:
		result.workspaceInput.action =
			WorkspaceInputAction::Activate;

		break;

	case KeyboardInput::KEY_Q:
		result.workspaceInput.action =
			WorkspaceInputAction::Back;

		break;

	case KeyboardInput::KEY_TAB:
	case KeyboardInput::KEY_SPACE:
		result.workspaceInput.action =
			WorkspaceInputAction::Toggle;

		break;

	case KeyboardInput::KEY_PLUS:
		result.workspaceInput.action =
			WorkspaceInputAction::ZoomIn;

		break;

	case KeyboardInput::KEY_MINUS:
		result.workspaceInput.action =
			WorkspaceInputAction::ZoomOut;

		break;

	case KeyboardInput::KEY_0:
		result.workspaceInput.action = WorkspaceInputAction::Option0;
		break;

	case KeyboardInput::KEY_1:
		result.workspaceInput.action = WorkspaceInputAction::Option1;
		break;

	case KeyboardInput::KEY_2:
		result.workspaceInput.action = WorkspaceInputAction::Option2;
		break;

	case KeyboardInput::KEY_3:
		result.workspaceInput.action = WorkspaceInputAction::Option3;
		break;

	case KeyboardInput::KEY_4:
		result.workspaceInput.action = WorkspaceInputAction::Option4;
		break;

	case KeyboardInput::KEY_5:
		result.workspaceInput.action = WorkspaceInputAction::Option5;
		break;

	case KeyboardInput::KEY_6:
		result.workspaceInput.action = WorkspaceInputAction::Option6;
		break;

	case KeyboardInput::KEY_7:
		result.workspaceInput.action = WorkspaceInputAction::Option7;
		break;

	case KeyboardInput::KEY_8:
		result.workspaceInput.action = WorkspaceInputAction::Option8;
		break;
	default:
		result.workspaceInput.action =
			WorkspaceInputAction::None;

		break;
	}

	if (result.workspaceInput.action !=
		WorkspaceInputAction::None) {

		result.hasWorkspaceInput = true;
		result.handled = true;
	}

	return result;
}

// =============================================================================
// MOUSE BUTTON ROUTING
// =============================================================================
TheArbiter::ArbiterResult
TheArbiter::routeMouseButton(
	int button,
	int state,
	int x,
	int y) const {

	ArbiterResult result;

	result.workspaceInput.x = x;
	result.workspaceInput.y = y;

	result.workspaceInput.button = button;
	result.workspaceInput.state = state;

	// ---------------------------------------------------------
	// Preserve right-click for the GLUT context menu.
	// ---------------------------------------------------------
	if (button == GLUT_RIGHT_BUTTON) {

		return result;
	}


	// ---------------------------------------------------------
	// Mouse wheel.
	//
	// FreeGLUT commonly reports these as buttons 3 / 4.
	// ---------------------------------------------------------

	if (button == 3) {

		result.workspaceInput.action =
			WorkspaceInputAction::ZoomIn;

		result.hasWorkspaceInput = true;
		result.handled = true;

		return result;
	}

	if (button == 4) {

		result.workspaceInput.action =
			WorkspaceInputAction::ZoomOut;

		result.hasWorkspaceInput = true;
		result.handled = true;

		return result;
	}

	// ---------------------------------------------------------
	// Pointer button.
	// ---------------------------------------------------------
	if (button == GLUT_LEFT_BUTTON) {

		if (state == GLUT_DOWN) {

			result.workspaceInput.action =
				WorkspaceInputAction::PointerDown;
		}
		else {

			result.workspaceInput.action =
				WorkspaceInputAction::PointerUp;
		}

		result.hasWorkspaceInput = true;
		result.handled = true;
	}

	return result;
}

// =============================================================================
// POINTER MOTION
// =============================================================================
TheArbiter::ArbiterResult
TheArbiter::routePointerMove(int x, int y) const {

	ArbiterResult result;

	result.workspaceInput.action =
		WorkspaceInputAction::PointerMove;

	result.workspaceInput.x = x;
	result.workspaceInput.y = y;

	result.hasWorkspaceInput = true;
	result.handled = true;

	return result;
}

void TheArbiter::requestEnterDomain(WorkspaceDomain domain) {

	m_navigationRequest.type =
		NavigationRequestType::ENTER_DOMAIN;

	m_navigationRequest.domain =
		domain;
}

void TheArbiter::requestReturnToGlobalShell(WorkspaceDomain domain) {

	m_navigationRequest.type =
		NavigationRequestType::RETURN_GLOBAL_SHELL;

	m_navigationRequest.domain =
		domain;
}

void TheArbiter::requestEnterWorkspaceConfiguration(
	WorkspaceDomain domain,
	WorkspaceId workspace) {

	m_navigationRequest.type =
		NavigationRequestType::ENTER_WORKSPACE_CONFIGURATION;

	m_navigationRequest.domain = domain;
	m_navigationRequest.workspace = workspace;
}

void TheArbiter::requestReturnToDomainSelection(
	WorkspaceDomain domain,
	WorkspaceId workspace) {

	m_navigationRequest.type =
		NavigationRequestType::RETURN_DOMAIN_SELECTION;

	m_navigationRequest.domain = domain;
	m_navigationRequest.workspace = workspace;
}

void TheArbiter::requestEnterActiveWorkspace(
	WorkspaceDomain domain,
	WorkspaceId workspace) {
	m_navigationRequest.type =
		NavigationRequestType::ENTER_ACTIVE_WORKSPACE;
	m_navigationRequest.domain = domain;
	m_navigationRequest.workspace = workspace;
}

void TheArbiter::requestReturnToWorkspaceConfiguration(
	WorkspaceDomain domain,
	WorkspaceId workspace) {
	m_navigationRequest.type =
		NavigationRequestType::RETURN_WORKSPACE_CONFIGURATION;
	m_navigationRequest.domain = domain;
	m_navigationRequest.workspace = workspace;
}

TheArbiter::NavigationRequest 
TheArbiter::takeNavigationRequest() {

	const NavigationRequest request =
		m_navigationRequest;

	m_navigationRequest =
		NavigationRequest{};

	return request;
}

// =============================================================================
// GENERIC STRUCTURAL QUERIES
// =============================================================================
bool TheArbiter::isGlobalShell() const {
	return
		m_navigation.layer ==
		ApplicationLayer::GLOBAL_SHELL;
}

bool TheArbiter::isDomainSelection() const {
	return
		m_navigation.layer ==
		ApplicationLayer::DOMAIN_SELECTION;
}

bool TheArbiter::isWorkspaceConfiguration() const {
	return
		m_navigation.layer ==
		ApplicationLayer::WORKSPACE_CONFIGURATION;
}

bool TheArbiter::isActiveWorkspace() const {
	return
		m_navigation.layer ==
		ApplicationLayer::ACTIVE_WORKSPACE;
}
