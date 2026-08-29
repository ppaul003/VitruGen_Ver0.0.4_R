#ifndef VITRUGEN_GRID_2D_WORKSPACE_H
#define VITRUGEN_GRID_2D_WORKSPACE_H

#include "IWorkspace.h"
#include "StaticParticleAssetIO.h"
#include "TheArbiter.h"

#include <cstdint>
#include <string>
#include <vector>

class Grid2DWorkspace : public IWorkspace {
public:
	enum class HostRequestType {
		None = 0,
		RefreshOutputCatalog,
		LoadTarget
	};

	struct HostRequest {
		HostRequestType type = HostRequestType::None;
		vitru::StaticAssetCatalogEntry target;
		vitru::AssetId replaceAssetId = vitru::INVALID_ASSET_ID;
	};

	bool initialize(WorkspaceServices& services) override;
	void enter(WorkspaceServices& services) override;
	void exit(WorkspaceServices& services) override;
	void update(const WorkspaceFrameContext& frame, WorkspaceServices& services) override;
	void render(const WorkspaceFrameContext& frame, WorkspaceServices& services) override;
	bool handleInput(const WorkspaceInputEvent& input, WorkspaceServices& services) override;
	WorkspacePresentation buildPresentation() const override;

	WorkspacePresentation buildLayer1TransitionPresentation() const;
	TheArbiter::WorkspaceId selectedWorkspaceId() const;
	bool automaticTransitionActive() const { return m_targetSweepActive; }

	HostRequest takeHostRequest();
	void replaceOutputCatalog(std::vector<vitru::StaticAssetCatalogEntry> catalog);
	void completeTargetLoad(
		bool loaded,
		bool ready,
		vitru::AssetId assetId,
		const std::string& displayName,
		const std::string& message);

private:
	enum class WorkspaceChoice { Graph2D = 0, TextureMap2D, Count };
	enum class Layer1Row { Workspace = 0, Target, LoadTarget, Configure, Count };
	enum class Layer2Row { PreviewRadius = 0, PixelGrid, RunWorkspaceEdit, Count };

	WorkspacePresentation buildLayer1Presentation() const;
	WorkspacePresentation buildLayer2Presentation() const;
	void moveLayer1Cursor(int direction);
	void adjustLayer1Value(int direction, WorkspaceServices& services);
	void activateLayer1(WorkspaceServices& services);
	void adjustLayer2Value(int direction);
	const char* workspaceName() const;
	const vitru::StaticAssetCatalogEntry* selectedTarget() const;
	void invalidateTarget();

private:
	static constexpr float kTargetSweepSpeed = 1.40f;

	TheArbiter* m_arbiter = nullptr;
	WorkspaceChoice m_workspace = WorkspaceChoice::Graph2D;
	Layer1Row m_layer1Row = Layer1Row::Workspace;
	Layer2Row m_layer2Row = Layer2Row::PreviewRadius;

	std::vector<vitru::StaticAssetCatalogEntry> m_outputCatalog;
	std::size_t m_selectedTargetIndex = 0;
	bool m_catalogReady = false;

	bool m_targetLoaded = false;
	bool m_targetReady = false;
	vitru::AssetId m_targetAssetId = vitru::INVALID_ASSET_ID;
	std::string m_loadedTargetName;
	std::string m_statusMessage;

	float m_previewParticleRadius = 0.0098f;
	std::uint32_t m_pixelGridDivisions = 64;

	// 1 = +Z/front plane only; 0 = -Z/rear plane plus full grid.
	float m_planeProgress = 1.0f;
	bool m_targetSweepActive = false;

	HostRequest m_pendingHostRequest;
};

#endif
