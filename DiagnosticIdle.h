#ifndef DIAGNOSTICS_IDLE_H
#define DIAGNOSTICS_IDLE_H

#include "IWorkspace.h"

class DiagnosticIdle :
	public IWorkspace {

public:
	bool initialize(WorkspaceServices& services) override;

	void enter(WorkspaceServices& services) override;

	void exit(WorkspaceServices& services) override;

	void update(
		const WorkspaceFrameContext& frame,
		WorkspaceServices& services
	) override;

	void render(
		const WorkspaceFrameContext& frame,
		WorkspaceServices& services
	) override;

	bool handleInput(
		const WorkspaceInputEvent& input,
		WorkspaceServices& services
	) override;

	WorkspacePresentation
		buildPresentation() const override;

private:
	float m_rotation = 0.0f;

	float m_sliceXY = -1.0f;
	float m_sliceXZ = -1.0f;
	float m_sliceYZ = -1.0f;
};

#endif
