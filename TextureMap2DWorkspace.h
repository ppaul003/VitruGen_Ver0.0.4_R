#ifndef VITRUGEN_TEXTURE_MAP_2D_WORKSPACE_H
#define VITRUGEN_TEXTURE_MAP_2D_WORKSPACE_H

#include "IWorkspace.h"
#include "StaticParticleAssetIO.h"
#include "TheArbiter.h"

#include <cstdint>
#include <string>
#include <vector>

class TextureMap2DWorkspace : public IWorkspace {
public:

	enum class Layer1Item {
		Workspace = 0,
		Target,
		LoadTarget,
		Configure,
		Count
	};

	enum class Layer2Item {
		PreviewRadius = 0,
		PixelGrid,
		RunWorkspaceEdit,
		Count
	};

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

public:
	TextureMap2DWorkspace();
	~TextureMap2DWorkspace() override;

	bool initialize(WorkspaceServices& services) override;
	void enter(WorkspaceServices& services) override;
	void exit(WorkspaceServices& services) override;
	void update(const WorkspaceFrameContext& frame, WorkspaceServices& services) override;
	void render(const WorkspaceFrameContext& frame, WorkspaceServices& services) override;
	bool handleInput(const WorkspaceInputEvent& input, WorkspaceServices& services) override;
	
	WorkspacePresentation buildPresentation() const override;
	WorkspacePresentation buildLayer1TransitionPresentation() const;
	bool automaticTransitionActive() const { return m_targetSweepActive; }

	HostRequest takeHostRequest();
	void replaceOutputCatalog(std::vector<vitru::StaticAssetCatalogEntry> catalog);
	
	void completeTargetLoad(
		bool loaded,
		bool ready,
		vitru::AssetId assetId,
		const std::string& displayName,
		const std::string& message
	);

private:
	WorkspacePresentation buildLayer1Presentation() const;
	WorkspacePresentation buildLayer2Presentation() const;
	void moveLayer1Cursor(int direction);
	void adjustLayer1Value(int direction, WorkspaceServices& services);
	void activateLayer1(WorkspaceServices& services);
	void adjustLayer2Value(int direction);

	const vitru::StaticAssetCatalogEntry* selectedTarget() const;
	void invalidateTarget();

private:
	static constexpr float kTargetSweepSpeed = 1.40f;

	TheArbiter* m_arbiter = nullptr;
	Layer1Item m_layer1Item = Layer1Item::Workspace;
	Layer2Item m_layer2Item = Layer2Item::PreviewRadius;

	bool m_targetLoaded = false;
	bool m_targetReady = false;

	std::vector<vitru::StaticAssetCatalogEntry> m_outputCatalog;
	std::size_t m_selectedTargetIndex = 0;
	bool m_catalogReady = false;

	vitru::AssetId m_targetAssetId = vitru::INVALID_ASSET_ID;
	std::string m_loadedTargetName;
	std::string m_statusMessage;

	float m_previewParticleRadius = 0.0098f;
	std::uint32_t m_pixelGridDivisions = 64;

	// 1 = +Z/front plane only; 0 = -Z/rear plane plus full grid.
	float m_planeProgress = 1.0f;
	bool m_targetSweepActive = false;
	bool m_targetCameraCenterPending = false;

	HostRequest m_pendingHostRequest;
};

#endif

