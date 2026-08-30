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
#include "TextureMap2DWorkspace.h"

struct cudaGraphicsResource;

class Tesseract {
public:
	enum class DomainTransitionPhase {
		NONE = 0,

		ENTER_DOMAIN_VISUAL,
		ENTER_DOMAIN_READY,

		// GRID_3D
		ENTER_CAMERA,

		ENTER_WORKSPACE_CAMERA,

		// GRID_2D
		ENTER_2D_CAMERA_Z,
		ENTER_2D_CAMERA_X,

		// GRID_3D
		EXIT_CAMERA,

		// GRID_2D
		EXIT_2D_CAMERA_X,
		EXIT_2D_CAMERA_Z,

		EXIT_DOMAIN_VISUAL,
		EXIT_WORKSPACE_CAMERA
	};

public:
	bool initialize(WorkspaceServices services);
	void shutdown();

	void update(const WorkspaceFrameContext& frame);
	void render(const WorkspaceFrameContext& frame);

	bool handleInput(const WorkspaceInputEvent& event);
	bool allowsInputRepeat(const WorkspaceInputEvent& event) const;

	WorkspacePresentation
		presentation() const;
	
	// --- SINGLE_PARTICLE --- //
	WorkspaceMenuPresentation menu() const;
	bool handleMenuCommand(int command);
	SingleParticleWorkspace::HostRequest takeSingleParticleHostRequest();

	TextureMap2DWorkspace::HostRequest takeTextureMap2DHostRequest();
	void replaceTextureMap2DOutputCatalog(std::vector<vitru::StaticAssetCatalogEntry> catalog);
	void replaceTextureMap2DBaseMaterialCatalog(
		std::vector<vitru::BaseMaterialCatalogEntry> catalog);

	void completeTextureMap2DTargetLoad(
		bool loaded,
		bool ready,
		vitru::AssetId assetId,
		const std::string& displayName,
		const std::string& message
	);
	TextureMap2DWorkspace* textureMap2DWorkspace() {
		return &m_texMap2DWorkspace;
	}
	const TextureMap2DWorkspace* textureMap2DWorkspace() const {
		return &m_texMap2DWorkspace;
	}

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
	static constexpr float kGrid3DCamTransCenter2D = 0.45f;
	static constexpr float kGrid3DCamTransStandard2D = 0.30f;

	DiagnosticIdle m_diagnosticIdle;

	WorkspaceServices m_services;
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
