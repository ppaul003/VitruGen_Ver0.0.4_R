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
#include "Graph3DWorkspace.h"
#include "SingleParticleWorkspace.h"
#include "LinkedParticlesWorkspace.h"
#include "Graph2DWorkspace.h"
#include "TextureMap2Dworkspace.h"

struct cudaGraphicsResource;

class Tesseract {
public:
	enum class DomainTransitionPhase {
		NONE = 0,

		ENTER_DOMAIN_VISUAL,
		ENTER_DOMAIN_READY,
		ENTER_CAMERA,
		ENTER_WORKSPACE_CAMERA,

		EXIT_CAMERA,
		EXIT_DOMAIN_VISUAL,
		EXIT_WORKSPACE_CAMERA
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

	TextureMap2DWorkspace::HostRequest takeTextureMap2DHostRequest();
	void replaceTextureMap2DOutputCatalog(std::vector<vitru::StaticAssetCatalogEntry> catalog);

	void completeTextureMap2DTargetLoad(
		bool loaded,
		bool ready,
		vitru::AssetId assetId,
		const std::string& displayName,
		const std::string& message
	);

	bool exportSingleParticleVolume(std::vector<float>& output) const;
	bool restoreSingleParticleVolume(const std::vector<float>& input, const int3& size);
	const int3& singleParticleVolumeSize() const;
	MarchingCubes* singleParticleMarchingCubes() const;
	bool prepareSingleParticleMarchingCubesExport();
	void activateSingleParticleExportedMeshRender();
	bool singleParticleHasCommittedGeometry() const;
	float singleParticleRadius() const;
	void activateLoadedSingleParticleBase(bool editableVolumeRestored);

	void processNavigationRequest();
	void updateDomainTransition(const WorkspaceFrameContext& frame);
	bool domainTransitionActive() const { return m_domainTransitionPhase != DomainTransitionPhase::NONE; }
	bool workspaceInputLocked() const {
		return domainTransitionActive() || m_texMap2DWorkspace.automaticTransitionActive();
	}

private:
	void synchronizeActiveCartridge();
	void activateCartridge(IWorkspace* workspace, const char* name);

private:
	static constexpr float kGrid3DCamTransDuration = 0.75f;
	static constexpr float kGrid3DReadyHoldDuration = 0.35f;
	
	WorkspaceServices m_services;
	DiagnosticIdle m_diagnosticIdle;
	//Grid2DWorkspace m_grid2DWorkspace;

	ParticleSimWorkspace m_particleSimWorkspace; // OFFLINE
	Graph3DWorkspace m_graph3DWorkspace; // OFFLINE
	SingleParticleWorkspace m_singleParticleWorkspace;
	LinkedParticlesWorkspace m_linkedParticlesWorkspace;
	Graph2DWorkspace m_graph2DWorkspace; // OFFLINE
	TextureMap2DWorkspace m_texMap2DWorkspace;

	DomainTransitionPhase m_domainTransitionPhase =
		DomainTransitionPhase::NONE;

	TheArbiter::WorkspaceDomain m_transitionDomain =
		TheArbiter::WorkspaceDomain::NONE;

	float m_transitionPhaseElapsed = 0.0f;

	IWorkspace* m_activeWorkspace = nullptr;
};

#endif
