#ifndef _THE_TESSERACT_H_
#define _THE_TESSERACT_H_

#include <GL/glew.h>
#include <cstddef>
#include <vector>
#include <vector_types.h>
#include <filesystem>

#include "IWorkspace.h"
#include "WorkspaceContext.h"
#include "WorkspaceInput.h"
#include "WorkspacePresentation.h"

#include "TheArbiter.h"
#include "renderer_Euclid.h"

#include "DiagnosticIdle.h"
#include "ParticleSimWorkspace.h"
#include "SingleParticleWorkspace.h"

struct cudaGraphicsResource;

class Tesseract {
public:
	enum class DomainTransitionPhase {
		NONE = 0,

		ENTER_DOMAIN_VISUAL,
		ENTER_CAMERA,

		EXIT_CAMERA,
		EXIT_DOMAIN_VISUAL
	};

public:
	bool initialize(WorkspaceServices services);
	void shutdown();

	void update(const WorkspaceFrameContext& frame);
	void render(const WorkspaceFrameContext& frame);

	bool handleInput(const WorkspaceInputEvent& event);

	WorkspacePresentation
		presentation() const;
	
	// --- SINGLE_PARTICLE --- //
	WorkspaceMenuPresentation menu() const;
	bool handleMenuCommand(int command);
	SingleParticleWorkspace::HostRequest takeSingleParticleHostRequest();
	bool exportSingleParticleVolume(std::vector<float>& output) const;
	bool restoreSingleParticleVolume(const std::vector<float>& input, const int3& size);
	const int3& singleParticleVolumeSize() const;
	MarchingCubes* singleParticleMarchingCubes() const;
	bool singleParticleHasCommittedGeometry() const;
	float singleParticleRadius() const;
	void activateLoadedSingleParticleBase(bool editableVolumeRestored);

	void processNavigationRequest();
	void updateDomainTransition(const WorkspaceFrameContext& frame);
	bool domainTransitionActive() const { return m_domainTransitionPhase != DomainTransitionPhase::NONE; }

private:
	void synchronizeActiveCartridge();
	void activateCartridge(IWorkspace* workspace, const char* name);

private:
	WorkspaceServices m_services;
	DiagnosticIdle m_diagnosticIdle;

	DomainTransitionPhase m_domainTransitionPhase =
		DomainTransitionPhase::NONE;

	TheArbiter::WorkspaceDomain m_transitionDomain =
		TheArbiter::WorkspaceDomain::NONE;

	ParticleSimWorkspace m_particleSimWorkspace;
	SingleParticleWorkspace m_singleParticleWorkspace;

	IWorkspace* m_activeWorkspace = nullptr;
};

#endif
