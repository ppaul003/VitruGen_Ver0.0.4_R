#ifndef VITRUGEN_TEXTURE_MAP_2D_WORKSPACE_H
#define VITRUGEN_TEXTURE_MAP_2D_WORKSPACE_H

#include "IWorkspace.h"
#include "PngImage.h"
#include "TheArbiter.h"
#include "ProjectAssetRepository.h"
#include "StaticParticleAssetIO.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace vitru {

    enum class TextureMapFocus { TextureCanvas = 0, MeshInspection };
    enum class TextureMapSubLayer { CycleSetup = 0, BranchSetup, PixelEditor, CommitSave };
    enum class TextureMapAuthoringMode { Coloring = 0, Contour, PanelLines };
    enum class TextureMapPreviewSource { Working = 0, Committed };
    enum class TextureMapViewMode { Edit = 0, Preview };
    enum class TextureMapContourAction { New = 0, EditExisting };
    enum class TextureMapChannel {
        BaseColor = 0,
        EmissiveColor,
        AlphaMask,
        Height,
        Normal,
        MetallicRoughness,
        Occlusion,
        Count
    };

    enum class TextureMap2DWorkspaceAction {
        None = 0,
        RequestSurfaceTargetName,
        RequestSaveCurrent,
        RequestSaveAs,
        StateChanged,
        Rejected
    };

    struct TextureMapGridCell {
        int x = -1;
        int y = -1;
        bool valid() const { return x >= 0 && y >= 0; }
        bool operator==(const TextureMapGridCell& other) const {
            return x == other.x && y == other.y;
        }
    };

    struct TextureMapLineColorPreset {
        const char* name = "BLACK";
        std::array<std::uint8_t, 4> rgba{ 0u, 0u, 0u, 255u };
    };

    enum class TextureTargetReadiness {
        NoTarget = 0,
        Ready,
        NeedsUv,
        NeedsTexture,
        Invalid
    };

    struct BaseMaterialCatalogEntry {
        std::string id;
        std::string displayName;

        std::filesystem::path rootPath;
        std::filesystem::path baseColorPath;

        std::uint32_t width = 0;
        std::uint32_t height = 0;

        bool valid = false;
        std::string status;
    };

    struct TextureDirtyRegion {
        bool valid = false;

        std::uint32_t minimumX = 0;
        std::uint32_t minimumY = 0;
        std::uint32_t maximumX = 0;
        std::uint32_t maximumY = 0;

        void clear() {
            valid = false;
            minimumX = minimumY = 0;
            maximumX = maximumY = 0;
        }
    };

    struct TextureMapTargetContext {
        AssetId assetId = INVALID_ASSET_ID;

        std::size_t submeshIndex = 0;
        std::size_t materialIndex = 0;

        TextureUsage channel = TextureUsage::BaseColor;
        std::string textureId;

        float previewParticleRadius = 0.0098f;
        std::uint32_t pixelGridDivisions = 64;

        TextureTargetReadiness readiness =
            TextureTargetReadiness::NoTarget;

        bool loaded = false;
    };

    struct TextureMapEditSession {
        bool active = false;
        bool dirty = false;

        AssetId assetId = INVALID_ASSET_ID;

        std::size_t submeshIndex = 0;
        std::size_t materialIndex = 0;

        std::string textureId;

        TextureMapAuthoringMode authoringMode =
            TextureMapAuthoringMode::Coloring;

        TextureMapViewMode viewMode = TextureMapViewMode::Edit;
        TextureMapChannel channel = TextureMapChannel::BaseColor;
        TextureMapContourAction contourAction = TextureMapContourAction::New;

        int selectedSurfaceTarget = -1;
        int selectedContourTarget = -1;
        std::uint32_t selectedFace = 0u;

        std::array<std::uint8_t, 4> paintColor{
            255u, 255u, 255u, 255u
        };

        int lineThickness = 1;
        std::size_t lineColorPreset = 0;
        float emissiveIntensity = 1.0f;

        std::vector<TextureMapGridCell> contourCells;
        bool contourClosed = false;

        TextureMapGridCell cursorCell;
        TextureMapGridCell previousStrokeCell;
        bool strokeActive = false;
		float editorZoom = 1.0f;

        // Snapshot used by Discard Changes.
        ImageRGBA8 originalImage;

        // Source material selected in Sub-Layer 0.
        ImageRGBA8 baseMaterialImage;

        // Transparent authoring layer used by panel lines.
        ImageRGBA8 panelLineOverlay;

        // Base material + tint + panel-line overlay.
        ImageRGBA8 compositeImage;

        // The active Base Color or Emissive texture edited by the
        // shared logical pixel-grid runtime.
        ImageRGBA8 workingImage;

        std::array<float, 4> tint{
            1.0f,
            1.0f,
            1.0f,
            1.0f
        };

        TextureDirtyRegion dirtyRegion;
        std::uint64_t revision = 0;

        void clear();
    };

    class TextureMap2DWorkspace final : public ::IWorkspace {
    public:
        enum class Layer1Item { Workspace = 0, Target, LoadTarget, Configure, Count };
        enum class Layer2Item { PreviewRadius = 0, PixelGrid, RunWorkspaceEdit, Count };
        enum class RuntimeGate { ReferencePreview = 0, SelectionArmed, TargetSelected, Authoring };
        enum class HostRequestType {
            None = 0,
            RefreshOutputCatalog,
            LoadTarget,
            RefreshBaseMaterialCatalog,
            SaveCurrent,
            SaveAs,
            RequestSurfaceTargetName,
            RequestSurfaceTargetNameThenSaveAs
        };
        struct HostRequest {
            HostRequestType type = HostRequestType::None;
            StaticAssetCatalogEntry target;
            AssetId replaceAssetId = INVALID_ASSET_ID;
            std::string suggestedName;
        };

        TextureMap2DWorkspace() = default;
        ~TextureMap2DWorkspace() override = default;

        bool initialize(::WorkspaceServices& services) override;
        void enter(::WorkspaceServices& services) override;
        void exit(::WorkspaceServices& services) override;
        void update(const ::WorkspaceFrameContext& frame, ::WorkspaceServices& services) override;
        void render(const ::WorkspaceFrameContext& frame, ::WorkspaceServices& services) override;
        bool handleInput(const ::WorkspaceInputEvent& input, ::WorkspaceServices& services) override;
        bool allowsInputRepeat(const ::WorkspaceInputEvent& input) const override;
        ::WorkspacePresentation buildPresentation() const override;
        ::WorkspaceMenuPresentation buildMenu() const override;
        bool handleMenuCommand(int command, ::WorkspaceServices& services) override;

        HostRequest takeHostRequest();
        void replaceOutputCatalog(std::vector<StaticAssetCatalogEntry> catalog);
        void completeTargetLoad(bool loaded, bool ready, AssetId assetId,
            const std::string& displayName, const std::string& message);
        bool completeTextEntry(const std::string& text, bool saveAsName,
            std::string* diagnostic = nullptr);
        void completeSaveRequest(bool succeeded, AssetId adoptedAssetId,
            const std::string& message);
        bool automaticTransitionActive() const;
        ::WorkspacePresentation buildLayer1TransitionPresentation() const;
        bool initialize(
            ProjectAssetRepository* repository,
            const std::filesystem::path& outputStaticParticlesRoot,
            const std::filesystem::path& baseMaterialsRoot
        );

        void reset();

        bool refreshOutputCatalog();
        void replaceBaseMaterialCatalog(
            std::vector<BaseMaterialCatalogEntry> catalog);

        bool selectOutputAsset(int direction);
        bool selectBaseMaterial(int direction);

        const StaticAssetCatalogEntry* selectedOutputAsset() const;
        const BaseMaterialCatalogEntry* selectedBaseMaterial() const;
		bool hasSelectedBaseMaterialSource() const {
			return m_baseMaterialSourceSelected;
		}

        // ---------------------------------------------------------
        // OUTPUT Static Particle catalog presentation state.
        //
        // TextureMap2DWorkspace retains ownership of the catalog.
        // External systems receive read-only state only.
        // ---------------------------------------------------------
        std::size_t outputCatalogCount() const { return m_outputCatalog.size(); }
        std::size_t selectedOutputIndex() const { return m_selectedOutputIndex; }

        bool hasOutputAssets() const {return !m_outputCatalog.empty(); }
        bool outputCatalogReady() const { return m_outputCatalogReady; }
        bool activateLoadedTarget(AssetId assetId);
        bool refreshTargetContext();

        bool beginEditSession();
        bool cancelEditSession();
        bool applyEditSessionToAsset();
        bool adjustPreviewParticleRadius(int direction);
        bool adjustPixelGridDivisions(int direction);

        // Layer 3 authoring runtime. Keyboard/menu input is reduced to
        // row selection plus these common commands; authoring data stays
        // entirely inside this workspace until an explicit commit.
        bool beginAuthoringRuntime();
        int runtimeRowCount() const;
        bool adjustRuntimeValue(int row, int direction);
        TextureMap2DWorkspaceAction activateRuntimeRow(
            int row,
            std::string* diagnostic = nullptr);

        bool toggleRuntimeView();
		bool adjustEditorZoom(int direction);
        bool canExitLayer3(std::string* diagnostic = nullptr) const;
        bool completeSurfaceTargetName(
            const std::string& name,
            std::string* diagnostic = nullptr);
        bool validateNewSurfaceTargetName(
            const std::string& name,
            std::string* diagnostic = nullptr) const;

        bool commitWorkingEdit(
            const std::string& newSurfaceTargetName = std::string{},
            std::string* diagnostic = nullptr);

        bool buildSaveAsSnapshot(
            const std::string& assetName,
            StaticParticleAsset& output,
            std::string* diagnostic = nullptr,
            const std::string& uncommittedSurfaceTargetName = std::string{}) const;

        bool adoptSavedTarget(AssetId assetId);

        bool setCursorCell(int x, int y);
        bool beginAuthoringStroke(int x, int y);
        bool continueAuthoringStroke(int x, int y);
        bool endAuthoringStroke();
        bool addContourPoint(int x, int y);
        bool closeContour(std::string* diagnostic = nullptr);
        bool undoContourPoint();

        StaticParticleAsset buildPreviewAsset() const;
        bool previewUsesWorkingState() const {
            return m_previewSource == TextureMapPreviewSource::Working;
        }

        TextureMapAuthoringMode authoringMode() const { return m_authoringMode; }
        TextureMapPreviewSource previewSource() const { return m_previewSource; }
        TextureMapSubLayer runtimeSubLayer() const { return m_subLayer; }
        TextureMapViewMode viewMode() const { return m_session.viewMode; }
        TextureMapChannel selectedChannel() const { return m_selectedChannel; }
        TextureMapContourAction contourAction() const { return m_contourAction; }
        int selectedSurfaceTarget() const { return m_selectedSurfaceTarget; }
        int selectedContourTarget() const { return m_selectedContourTarget; }
        std::uint32_t selectedFace() const { return m_session.selectedFace; }
        const std::array<std::uint8_t, 4>& paintColor() const { return m_session.paintColor; }
        int lineThickness() const { return m_session.lineThickness; }
        std::size_t lineColorPresetIndex() const { return m_session.lineColorPreset; }
        float emissiveIntensity() const {
            return m_session.active ? m_session.emissiveIntensity : m_emissiveIntensity;
        }
		float editorZoom() const { return m_session.editorZoom; }
        bool contourClosed() const { return m_session.contourClosed; }
        std::size_t contourPointCount() const { return m_session.contourCells.size(); }
        bool nestedFocus() const { return m_nestedFocus; }
        const std::string& runtimeStatusMessage() const { return m_runtimeStatusMessage; }
        void clearRuntimeStatusMessage() { m_runtimeStatusMessage.clear(); }

        static const std::array<TextureMapLineColorPreset, 4>& lineColorPresets();
        static const char* boxAtlasFaceAxisName(std::uint32_t faceIndex);
        static bool channelActive(TextureMapChannel channel);

        ProjectAssetRepository* repository() { return m_repository; }
        const TextureMapTargetContext& target() const { return m_target; }

        TextureMapTargetContext& target() {return m_target;}
        const TextureMapEditSession& session() const { return m_session;}

        TextureMapEditSession& session() {return m_session; }
        TextureMapFocus focus() const { return m_focus; }

        void toggleFocus();
        TextureMapSubLayer subLayer() const { return m_subLayer; }

        void setSubLayer(TextureMapSubLayer value) { m_subLayer = value; }
        bool initialized() const { return m_initialized; }

    private:
        ::WorkspacePresentation buildLayer1Presentation() const;
        ::WorkspacePresentation buildLayer2Presentation() const;
        ::WorkspacePresentation buildLayer3Presentation() const;
        void moveLayer1Cursor(int direction);
        void adjustLayer1Value(int direction, ::WorkspaceServices& services);
        void activateLayer1(::WorkspaceServices& services);
        void moveLayer2Cursor(int direction);
        void activateLayer2(::WorkspaceServices& services);
        void resetLayer3Runtime();
        void moveRuntimeCursor(int direction);
        bool handleLayer3Input(const ::WorkspaceInputEvent& input, ::WorkspaceServices& services);
        void requestReturnToLayer2(::WorkspaceServices& services);
		void updateCameraIntent(::WorkspaceServices& services) const;
        const char* authoringModeName() const;
        const char* channelName() const;
        std::string selectedSurfaceName() const;
        std::string loadedTargetName() const;
        bool pointerToEditorCell(int x, int y, int& cellX, int& cellY) const;
        void reloadPreviewMesh(::WorkspaceServices& services) const;

    private:
        bool prepareWorkingPass(std::string* diagnostic = nullptr);
        bool abandonWorkingPass();
        bool applyCell(const TextureMapGridCell& cell, bool panelLine);
        bool rasterizeStroke(
            const TextureMapGridCell& from,
            const TextureMapGridCell& to,
            bool panelLine);
        bool cellInsideSelectedSurfaceTarget(const TextureMapGridCell& cell) const;
        bool contourHasSelfIntersection() const;
        ImageRGBA8 textureImage(const TextureResource* texture) const;
        TextureResource* ensureEditableTexture(
            StaticParticleAsset& asset,
            TextureMapChannel channel,
            const ImageRGBA8& image) const;
        ProjectAssetRepository* m_repository = nullptr;

        std::filesystem::path m_outputStaticParticlesRoot;
        std::filesystem::path m_baseMaterialsRoot;

        std::vector<StaticAssetCatalogEntry> m_outputCatalog;
        std::size_t m_selectedOutputIndex = 0;

        // True after OUTPUT/STATIC_PARTICLES has been scanned
        // successfully, even when the resulting catalog is empty.
        bool m_outputCatalogReady = false;

        std::vector<BaseMaterialCatalogEntry> m_baseMaterialCatalog;
        std::size_t m_selectedBaseMaterialIndex = 0;

        TextureMapTargetContext m_target;
        TextureMapEditSession m_session;

        TextureMapFocus m_focus =
            TextureMapFocus::TextureCanvas;

        TextureMapSubLayer m_subLayer = TextureMapSubLayer::CycleSetup;
        TextureMapAuthoringMode m_authoringMode = TextureMapAuthoringMode::Coloring;
        TextureMapPreviewSource m_previewSource = TextureMapPreviewSource::Working;
        TextureMapChannel m_selectedChannel = TextureMapChannel::BaseColor;
        TextureMapContourAction m_contourAction = TextureMapContourAction::New;
        int m_selectedSurfaceTarget = -1;
        int m_selectedContourTarget = -1;
        bool m_nestedFocus = false;
        bool m_baseMaterialSourceSelected = false;
        float m_emissiveIntensity = 1.0f;
        std::string m_runtimeStatusMessage;

        bool m_initialized = false;

        TheArbiter* m_arbiter = nullptr;
        Layer1Item m_layer1Item = Layer1Item::Workspace;
        Layer2Item m_layer2Item = Layer2Item::PreviewRadius;
        RuntimeGate m_runtimeGate = RuntimeGate::ReferencePreview;
        int m_runtimeRow = 0;
        bool m_runtimePanelVisible = false;
        int m_viewportWidth = 1920;
        int m_viewportHeight = 1080;
        float m_planeProgress = 1.0f;
        float m_planeTarget = 1.0f;
        bool m_planeAnimating = false;
		bool m_localCameraTransition = false;
		bool m_selectionPointerValid = false;
		int m_selectionPointerX = 0;
		int m_selectionPointerY = 0;
        bool m_targetCameraCenterPending = false;
        std::string m_loadedTargetDisplayName;
        std::string m_layerStatusMessage;
        HostRequest m_pendingHostRequest;
    };

} // namespace vitru

using TextureMap2DWorkspace = vitru::TextureMap2DWorkspace;
#endif
