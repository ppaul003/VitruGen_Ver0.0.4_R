#include "ParticleSimWorkspace.h"

#include "IWorkspace.h"

#include "renderer_Euclid.h"
#include "TheArbiter.h"

using namespace std;

bool ParticleSimWorkspace::initialize(
	WorkspaceServices& services) {

	if (m_initialized) return true;

	if (!services.renderer ||
		!services.arbiter) return false;

	m_radii.assign(kParticleCapacity, 0.0f);
	
	m_particleSystem = std::make_unique<ParticleSystem>(
		kParticleCapacity,
		m_gridDimensions,
		true);

	m_particleSystem->setSimulationDomain(4.0f);
	m_particleSystem->setDefaultColorRamp();
	m_particleSystem->reset(ParticleSystem::CNFG_DEFAULT_RESTART);

	m_initialized = true;
	return true;
}

void ParticleSimWorkspace::enter(
	WorkspaceServices& services) {

	if (!m_initialized) return;

	m_active = true;
}

void ParticleSimWorkspace::exit(
	WorkspaceServices& services) {

	m_active = false;
}

void ParticleSimWorkspace::update(
	const WorkspaceFrameContext& frame,
	WorkspaceServices& services) {

	if (!m_active) return;

}

void ParticleSimWorkspace::render(
	const WorkspaceFrameContext& frame,
	WorkspaceServices& services) {

	if (!m_active) return;
}

bool ParticleSimWorkspace::handleInput(
	const WorkspaceInputEvent& input,
	WorkspaceServices& services) {

	if (!m_active)
		return false;

	return false;
}

WorkspacePresentation
ParticleSimWorkspace::buildPresentation() const {

	WorkspacePresentation presentation;
	presentation.panelVisible = true;

	presentation.workspaceName =
		"PARTICLE_SIMULATION";

	presentation.layerLabel =
		"SIMCAD_4D";

	presentation.statusLine =
		m_active
		? "PARTICLE_SIM WORKSPACE ONLINE."
		: "PARTICLE_SIM WORKSPACE OFFLINE.";


	return presentation;
}

bool ParticleSimWorkspace::resolveRuntimeConfig(
	RuntimeConfig& resolved) const {

	const unsigned long long requestedCount =
		m_draftConfig.colorMode == ColorMode::Default
		? static_cast<unsigned long long>(m_draftConfig.defaultParticleCount)
		: static_cast<unsigned long long>(m_draftConfig.redCount) +
		static_cast<unsigned long long>(m_draftConfig.greenCount) +
		static_cast<unsigned long long>(m_draftConfig.blueCount);

	if (requestedCount > m_capacity)
		return false;

	const bool uniformRadiusValid =
		std::isfinite(m_draftConfig.uniformRadius) &&
		m_draftConfig.uniformRadius > 0.0f &&
		m_draftConfig.uniformRadius <= kMaximumSupportedRadius;

	const bool randomRadiusValid =
		std::isfinite(m_draftConfig.minimumRadius) &&
		std::isfinite(m_draftConfig.maximumRadius) &&
		m_draftConfig.minimumRadius > 0.0f &&
		m_draftConfig.minimumRadius <= m_draftConfig.maximumRadius &&
		m_draftConfig.maximumRadius <= kMaximumSupportedRadius;

	if (m_draftConfig.radiusMode == RadiusMode::Uniform) {
		if (!uniformRadiusValid) return false;
		
	}
	else if (m_draftConfig.radiusMode == RadiusMode::Random) {
		if (!randomRadiusValid) return false;

	}
	else {

		return false;
	}

	resolved.capacity = m_capacity;

	resolved.activeCount =
		static_cast<unsigned int>(requestedCount);

	resolved.colorMode =
		m_draftConfig.colorMode;

	resolved.radiusMode =
		m_draftConfig.radiusMode;

	resolved.resetMode =
		m_draftConfig.resetMode;

	resolved.uniformRadius =
		m_draftConfig.uniformRadius;

	resolved.minimumRadius =
		m_draftConfig.minimumRadius;

	resolved.maximumRadius =
		m_draftConfig.maximumRadius;

	resolved.placementRadius =
		m_draftConfig.radiusMode == RadiusMode::Random
		? m_draftConfig.maximumRadius
		: m_draftConfig.uniformRadius;

	if (m_draftConfig.colorMode == ColorMode::RGB) {

		resolved.redCount = m_draftConfig.redCount;
		resolved.greenCount = m_draftConfig.greenCount;
		resolved.blueCount = m_draftConfig.blueCount;
	}

	return true;
}