#ifndef VITRUGEN_IWORKSPACE_H
#define VITRUGEN_IWORKSPACE_H

#include "WorkspaceContext.h"
#include "WorkspaceInput.h"
#include "WorkspacePresentation.h"

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

	virtual WorkspacePresentation
		buildPresentation() const = 0;
};

#endif
