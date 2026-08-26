#ifndef __ARBITER_SYS_H__
#define __ARBITER_SYS_H__

#include <string>

#include "Interactions.h"

#include "WorkspaceInput.h"

class TheArbiter {
public:
	enum class ApplicationLayer {

		GLOBAL_SHELL = 0,
		DOMAIN_SELECTION,
		WORKSPACE_CONFIGURATION,
		ACTIVE_WORKSPACE
	};

	enum class WorkspaceDomain {
		NONE = 0,

		GRID_2D,
		GRID_3D,
		SIMCAD_4D
	};

	enum class NavigationRequestType {
		NONE = 0,

		ENTER_DOMAIN,
		RETURN_GLOBAL_SHELL
	};

	enum class WorkspaceId {
		NONE = 0,

		DIAGNOSTIC,

		GRAPH_2D,
		TEXTURE_MAP_2D,

		GRAPH_3D,
		SINGLE_PARTICLE_MCAD,
		LINKED_PARTICLES_MCAD,

		PARTICLE_SIMULATION,
		SANDBOX_SIMULATION
	};

	// =========================================================
	// GENERIC HOST COMMANDS
	//
	// These are commands for the APPLICATION HOST.
	//
	// Workspace actions such as:
	//
	//     change radius
	//     paint texture
	//     commit volume
	//     pause simulation
	//     select primitive
	//
	// do NOT belong here.
	//
	// Those go through WorkspaceInputEvent and are interpreted
	// by the active cartridge.
	// =========================================================
	enum class ArbiterCommand {
		CMD_NONE = 0,

		CMD_EXIT,
		CMD_REDRAW,
		CMD_MENU
	};

	struct NavigationRequest {

		NavigationRequestType type =
			NavigationRequestType::NONE;

		WorkspaceDomain domain =
			WorkspaceDomain::NONE;

	};

	// =========================================================
	// ARBITER ROUTING RESULT
	// =========================================================
	struct ArbiterResult {

		ArbiterCommand arbiterCommand =
			ArbiterCommand::CMD_NONE;

		bool handled = false;

		bool requestRedraw = false;
		bool rebuildMenu = false;

		bool hasWorkspaceInput = false;

		// --- WRITE/SAVE SP ASSETS ---
		bool exportObjRequested = false;
		bool saveStaticParticleAsRequested = false;
		bool loadStaticParticleRequested = false;
		std::string staticParticleAssetName;
		// --- \WRITE/SAVE SP ASSETS ---

		WorkspaceInputEvent workspaceInput;
	};

	// =========================================================
	// STRUCTURAL NAVIGATION STATE
	// =========================================================
	struct NavigationState {

		ApplicationLayer layer =
			ApplicationLayer::GLOBAL_SHELL;

		WorkspaceDomain domain =
			WorkspaceDomain::NONE;

		WorkspaceId workspace =
			WorkspaceId::DIAGNOSTIC;
	};

public:
	TheArbiter();
	~TheArbiter();

	// =========================================================
	// GENERIC INPUT ROUTING
	// =========================================================
	ArbiterResult routeKeyboard(const KeyboardInput::KeyEvent& event) const;

	ArbiterResult routeMouseButton(
		int button,
		int state,
		int x,
		int y
	) const;

	ArbiterResult routePointerMove(int x, int y) const;

	// =========================================================
	// STRUCTURAL NAVIGATION
	// =========================================================
	void setApplicationLayer(ApplicationLayer layer) { m_navigation.layer = layer; }
	ApplicationLayer getApplicationLayer() const { return m_navigation.layer; }

	void setWorkspaceDomain(WorkspaceDomain domain) { m_navigation.domain = domain; }
	WorkspaceDomain getWorkspaceDomain() const { return m_navigation.domain; }

	void setActiveWorkspace(WorkspaceId workspace) { m_navigation.workspace = workspace; }
	WorkspaceId getActiveWorkspace() const { return m_navigation.workspace; }

	const NavigationState& getNavigationState() const { return m_navigation; }

	void requestEnterDomain(WorkspaceDomain domain);
	void requestReturnToGlobalShell(WorkspaceDomain domain);
	bool hasNavigationRequest() const { return m_navigationRequest.type != NavigationRequestType::NONE; }
	NavigationRequest takeNavigationRequest();

	// =========================================================
	// GENERIC QUERY
	// =========================================================
	bool isGlobalShell() const;
	bool isDomainSelection() const;
	bool isWorkspaceConfiguration() const;
	bool isActiveWorkspace() const;

private:
	NavigationState m_navigation;
	NavigationRequest m_navigationRequest;
};

#endif
