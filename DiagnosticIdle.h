#ifndef DIAGNOSTICS_IDLE_H
#define DIAGNOSTICS_IDLE_H

#include "IWorkspace.h"

class TheArbiter;

class DiagnosticIdle :
	public IWorkspace {

public:
	enum class VisualTransitionState {
		Idle = 0,

		Grid3D_OrientToFront,
		Grid3D_CaptureSlice,
		Grid3D_HoldCenter,

		Grid3D_ReleaseSlice
	};

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

	void beginGrid3DEnterTransition();
	void beginGrid3DReturnTransition();

	bool grid3DEnterVisualComplete() const { return m_grid3DEnterComplete; }
	bool grid3DReturnVisualComplete() const { return m_grid3DReturnComplete; }

	float previewRotationDegrees() const { return m_previewRotationDegrees; }

private:
	const char* selectedEnvironmentName() const;
	void cycleEnvironment(int direction);

private:
	static constexpr float kPreviewRotationSpeed = 25.0f;
	static constexpr float kSliceCycleSpeed = 0.35f;

	// ---------------------------------------------------------
	// DOMAIN TRANSITION SPEEDS
	//
	// Faster than normal Layer-0 idle motion so AUTO:
	// transition visually reads as deliberate system control.
	// ---------------------------------------------------------
	static constexpr float kTransitionRotationSpeed = 120.0f;
	static constexpr float kTransitionSliceSpeed = 1.40f;

	TheArbiter* m_arbiter = nullptr;

	VisualTransitionState m_visualTransition =
		VisualTransitionState::Idle;

	bool m_grid3DEnterComplete = false;
	bool m_grid3DReturnComplete = false;

	float m_previewRotationDegrees = 0.0f;

	// Unwrapped rather than fmod'd.
	// This makes "wait for NEXT pass" easy to calculate.
	float m_sliceTravel = 0.0f;

	float m_targetRotationDegrees = 0.0f;
	float m_targetSliceTravel = 0.0f;

};

#endif
