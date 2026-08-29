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

		Grid3D_ReleaseSlice,

		Grid2D_OrientToFront,
		Grid2D_WaitForXYStart,
		Grid2D_SweepToFront,
		Grid2D_HoldFront,
		Grid2D_ReturnSweep
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
	void beginGrid2DEnterTransition();
	void beginGrid2DReturnTransition();

	bool grid3DEnterVisualComplete() const { return m_grid3DEnterComplete; }
	bool grid3DReturnVisualComplete() const { return m_grid3DReturnComplete; }
	bool grid2DEnterVisualComplete() const { return m_grid2DEnterComplete; }
	bool grid2DReturnVisualComplete() const { return m_grid2DReturnComplete; }

	float previewRotationDegrees() const { return m_previewRotationDegrees; }

private:
	const char* selectedEnvironmentName() const;
	void cycleEnvironment(int direction);
	
	void moveGlobalShellCursor(int direciton);
	void adjustGlobalShellValue(int direciton);

	bool globalShellConfigRead() const;
	const char* simulationBoxSizeName() const;

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

	enum class GlobalShellRow {
		Environment = 0,
		SimulationBox,
		GlobalIllumination,
		Configure,

		Count
	};

	GlobalShellRow m_activeShellRow =
		GlobalShellRow::Environment;

	TheArbiter* m_arbiter = nullptr;

	VisualTransitionState m_visualTransition =
		VisualTransitionState::Idle;

	bool m_grid3DEnterComplete = false;
	bool m_grid3DReturnComplete = false;
	bool m_grid2DEnterComplete = false;
	bool m_grid2DReturnComplete = false;

	int m_requestedSimBoxSize = 4;
	int m_globalIlluminationDeg = 45;

	float m_previewRotationDegrees = 0.0f;

	// Unwrapped rather than fmod'd.
	// This makes "wait for NEXT pass" easy to calculate.
	float m_sliceTravel = 0.0f;

	float m_targetRotationDegrees = 0.0f;
	float m_targetSliceTravel = 0.0f;
	float m_grid2DPlaneProgress = 0.0f;

};

#endif
