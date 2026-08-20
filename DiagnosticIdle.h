#ifndef DIAGNOSTICS_IDLE_H
#define DIAGNOSTICS_IDLE_H

#include "IWorkspace.h"

class TheArbiter;

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
	const char* selectedEnvironmentName() const;
	void cycleEnvironment(int direction);

	TheArbiter* m_arbiter = nullptr;

	// 0.0 -> 1.0 : XY sweep
	// 1.0 -> 2.0 : XZ sweep
	// 2.0 -> 3.0 : YZ sweep
	float m_sliceCycle = 0.0f;
};

#endif
