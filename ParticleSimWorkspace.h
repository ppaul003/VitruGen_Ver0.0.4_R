#ifndef VITRUGEN_PARTICLE_SIM_WORKSPACE_H
#define VITRUGEN_PARTICLE_SIM_WORKSPACE_H

#include <memory>
#include <vector>

#include <vector_types.h>
#include <vector_functions.h>
#include <cuda_runtime.h>

#include "IWorkspace.h"
#include "particleSystem.h"

class ParticleSimWorkspace :
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
	enum class GridLayout {
		None = 0,
		Minimal,
		Full,
		Dynamic
	};

	enum class ColorMode {
		Default = 0,
		RGB
	};

	enum class RadiusMode {
		Uniform = 0,
		Random
	};

	enum class ColorChannel {
		Red = 0,
		Green,
		Blue
	};

	enum class ResetMode {
		Default = 0,
		Random
	};

	struct DraftConfig {

		GridLayout gridLayout =
			GridLayout::Full;

		ColorMode colorMode =
			ColorMode::Default;

		RadiusMode radiusMode =
			RadiusMode::Uniform;

		unsigned int defaultParticleCount = 4200;

		unsigned int redCount = 0;
		unsigned int greenCount = 0;
		unsigned int blueCount = 0;

		ColorChannel selectedColorChannel =
			ColorChannel::Red;

		ResetMode resetMode =
			ResetMode::Default;

		float uniformRadius = 0.0120f;
		float minimumRadius = 0.0098f;
		float maximumRadius = 0.0156f;
	};

	struct RuntimeConfig {

		unsigned int capacity = 0;
		unsigned int activeCount = 0;

		unsigned int redCount = 0;
		unsigned int greenCount = 0;
		unsigned int blueCount = 0;

		float uniformRadius = 0.0120f;
		float minimumRadius = 0.0098f;
		float maximumRadius = 0.0156f;
		float placementRadius = 0.0120f;

		ColorMode colorMode =
			ColorMode::Default;

		RadiusMode radiusMode =
			RadiusMode::Uniform;

		ResetMode resetMode =
			ResetMode::Default;
	};

	bool resolveRuntimeConfig(RuntimeConfig& resolved) const;

private:
	static constexpr float
		kMaximumSupportedRadius = 0.0156f;

	static constexpr unsigned int
		kParticleCapacity = 16384;

	static constexpr unsigned int
		kGridSize = 64;

	unsigned int m_capacity =
		kParticleCapacity;

	unsigned int m_activeCount =
		kParticleCapacity;

	uint3 m_gridDimensions =
		make_uint3(
			kGridSize,
			kGridSize,
			kGridSize
		);

	std::unique_ptr<ParticleSystem> m_particleSystem;
	std::vector<float> m_radii;

	DraftConfig m_draftConfig;
	RuntimeConfig m_runtimeConfig;

	bool m_initialized = false;
	bool m_active = false;
	bool m_paused = true;

	float m_elapsedSimulationTime = 0.0f;
};

#endif
