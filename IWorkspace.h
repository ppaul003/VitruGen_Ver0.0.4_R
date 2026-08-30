#ifndef VITRUGEN_IWORKSPACE_H
#define VITRUGEN_IWORKSPACE_H

#include "WorkspaceContext.h"
#include "WorkspaceInput.h"
#include "WorkspacePresentation.h"
#include "WorkspaceMenu.h"

class IWorkspace {

public:
	virtual ~IWorkspace() = default;

	virtual bool initialize(WorkspaceServices& services) = 0;

	virtual void enter(WorkspaceServices& services) = 0;
	virtual void exit(WorkspaceServices& services) = 0;

	virtual void update(
		const WorkspaceFrameContext& frame,
		WorkspaceServices& services
	) = 0;

	virtual void render(
		const WorkspaceFrameContext& frame,
		WorkspaceServices& services
	) = 0;

	virtual bool handleInput(
		const WorkspaceInputEvent& input,
		WorkspaceServices& services
	) = 0;

	// The host owns physical key timing; the cartridge remains the authority
	// on where a repeated generic action has semantic meaning.
	virtual bool allowsInputRepeat(
		const WorkspaceInputEvent& input
	) const {
		(void)input;
		return false;
	}

	virtual WorkspacePresentation
		buildPresentation() const = 0;

	virtual WorkspaceMenuPresentation buildMenu() const {
		return WorkspaceMenuPresentation{};
	}

	virtual bool handleMenuCommand(int command, WorkspaceServices& services) {
		(void)command;
		(void)services;
		return false;
	}
};

#endif
