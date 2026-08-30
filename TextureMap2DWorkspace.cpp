#include "Camera.h"
#include "renderer_Euclid.h"
#include "TextureMap2DWorkspace.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <unordered_set>
#include <system_error>
#include <utility>

namespace vitru {

    namespace {

        constexpr std::array<float, 17>
            kPreviewParticleRadiusPresets{
                0.0039f,
                0.0046f,
                0.0054f,
                0.0061f,
                0.0068f,
                0.0076f,
                0.0083f,
                0.0091f,
                0.0098f,
                0.0105f,
                0.0113f,
                0.0120f,
                0.0127f,
                0.0135f,
                0.0142f,
                0.0149f,
                0.0156f
            };

        constexpr std::array<std::uint32_t, 3>
            kPixelGridDivisionPresets{
                32,
                64,
                128
            };

        std::string lowerAscii(std::string value) {

            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](unsigned char c) {
                    return static_cast<char>(
                        std::tolower(c)
                        );
                }
            );

            return value;
        }

        bool isVspaManifestPath(
            const std::filesystem::path& path) {

            const std::string filename =
                lowerAscii(
                    path.filename().string()
                );

            const std::string suffix =
                ".vspa.json";

            if (filename.size() < suffix.size()) {
                return false;
            }

            return filename.compare(
                filename.size() - suffix.size(),
                suffix.size(),
                suffix
            ) == 0;
        }

        std::string normalizedPathKey(
            const std::filesystem::path& path) {

            return lowerAscii(
                path
                .lexically_normal()
                .generic_string()
            );
        }

        int wrappedIndex(int value, int count) {
            if (count <= 0) return 0;
            return (value % count + count) % count;
        }

        bool validAuthoringName(const std::string& value) {
            if (value.empty()) return false;
            for (char c : value) {
                const unsigned char uc = static_cast<unsigned char>(c);
                if (!std::isalnum(uc) && c != '_' && c != '-') return false;
            }
            return true;
        }

        float orientation(
            const TextureMapGridCell& a,
            const TextureMapGridCell& b,
            const TextureMapGridCell& c) {

            return static_cast<float>(b.y - a.y) *
                static_cast<float>(c.x - b.x) -
                static_cast<float>(b.x - a.x) *
                static_cast<float>(c.y - b.y);
        }

        bool strictSegmentsIntersect(
            const TextureMapGridCell& a,
            const TextureMapGridCell& b,
            const TextureMapGridCell& c,
            const TextureMapGridCell& d) {

            const float abC = orientation(a, b, c);
            const float abD = orientation(a, b, d);
            const float cdA = orientation(c, d, a);
            const float cdB = orientation(c, d, b);
            return ((abC > 0.0f && abD < 0.0f) ||
                (abC < 0.0f && abD > 0.0f)) &&
                ((cdA > 0.0f && cdB < 0.0f) ||
                    (cdA < 0.0f && cdB > 0.0f));
        }

        bool pointInPolygon(
            float x,
            float y,
            const std::vector<Vec2>& polygon) {

            bool inside = false;
            if (polygon.size() < 3u) return false;
            for (std::size_t i = 0, j = polygon.size() - 1u;
                i < polygon.size();
                j = i++) {

                const Vec2& a = polygon[i];
                const Vec2& b = polygon[j];
                const bool crosses = ((a.y > y) != (b.y > y)) &&
                    (x < (b.x - a.x) * (y - a.y) /
                        ((b.y - a.y) == 0.0f ? 1.0e-12f : (b.y - a.y)) + a.x);
                if (crosses) inside = !inside;
            }
            return inside;
        }

    } // namespace

    void TextureMapEditSession::clear() {
        *this = TextureMapEditSession{};
    }


    bool TextureMap2DWorkspace::initialize(
        ProjectAssetRepository* repository,
        const std::filesystem::path& outputStaticParticlesRoot,
        const std::filesystem::path& baseMaterialsRoot) {

        // Do not leave a partially initialized workspace behind.
        m_initialized = false;
        m_repository = nullptr;

        m_outputStaticParticlesRoot.clear();
        m_baseMaterialsRoot.clear();

        if (!repository) {

            std::printf(
                "[TextureMap2DWorkspace] Initialization failed: "
                "ProjectAssetRepository is null.\n"
            );

            return false;
        }

        std::error_code error;

        const bool outputRootReady =
            std::filesystem::is_directory(
                outputStaticParticlesRoot,
                error
            );

        if (!outputRootReady || error) {

            std::printf(
                "[TextureMap2DWorkspace] Initialization failed: "
                "OUTPUT static-particle directory is unavailable: %s\n",
                outputStaticParticlesRoot.string().c_str()
            );

            return false;
        }

        error.clear();

        const bool materialRootReady =
            std::filesystem::is_directory(
                baseMaterialsRoot,
                error
            );

        if (!materialRootReady || error) {

            std::printf(
                "[TextureMap2DWorkspace] Initialization failed: "
                "base-material directory is unavailable: %s\n",
                baseMaterialsRoot.string().c_str()
            );

            return false;
        }

        m_repository = repository;

        m_outputStaticParticlesRoot =
            outputStaticParticlesRoot;

        m_baseMaterialsRoot =
            baseMaterialsRoot;

        // Initialize deterministic workspace defaults.
        m_outputCatalog.clear();
        m_baseMaterialCatalog.clear();

        m_selectedOutputIndex = 0;
        m_selectedBaseMaterialIndex = 0;

        m_outputCatalogReady = false;

        m_target =
            TextureMapTargetContext{};

        m_session =
            TextureMapEditSession{};

        m_focus =
            TextureMapFocus::TextureCanvas;

        m_subLayer =
            TextureMapSubLayer::CycleSetup;

        m_initialized = true;

        // Build the initial OUTPUT/STATIC_PARTICLES catalog.
        //
        // An empty catalog is valid.
        //
        // Failure to scan is non-fatal. refreshOutputCatalog()
        // remains the reusable internal rescan operation and will
        // later be invoked automatically when Layer 1 is re-entered.
        if (!refreshOutputCatalog()) {

            std::printf(
                "[TextureMap2DWorkspace] WARNING: "
                "initial OUTPUT catalog scan failed.\n"
            );
        }

        std::printf(
            "[TextureMap2DWorkspace] Runtime initialized.\n"
        );

        std::printf(
            "  OUTPUT catalog root: %s\n",
            m_outputStaticParticlesRoot.string().c_str()
        );

        std::printf(
            "  Base-material root: %s\n",
            m_baseMaterialsRoot.string().c_str()
        );

        return true;
    }

    bool TextureMap2DWorkspace::refreshOutputCatalog() {

        if (!m_initialized) {

            std::printf(
                "[TextureMap2DWorkspace] OUTPUT catalog refresh skipped: "
                "workspace is not initialized.\n"
            );

            return false;
        }

        std::error_code error;

        if (!std::filesystem::is_directory(
            m_outputStaticParticlesRoot,
            error) || error) {

            std::printf(
                "[TextureMap2DWorkspace] OUTPUT catalog refresh failed: %s\n",
                m_outputStaticParticlesRoot.string().c_str()
            );

            return false;
        }

        // ---------------------------------------------------------
        // Remember the currently selected manifest so a manual
        // refresh does not unnecessarily jump the user's selection.
        // ---------------------------------------------------------
        std::filesystem::path previousSelection;

        if (const StaticAssetCatalogEntry* selected =
            selectedOutputAsset()) {

            previousSelection =
                selected->manifestPath;
        }

        const std::string previousSelectionKey =
            normalizedPathKey(
                previousSelection
            );

        // Build into a temporary catalog.
        //
        // If scanning fails, the currently active catalog remains
        // untouched.
        std::vector<StaticAssetCatalogEntry> nextCatalog;

        std::filesystem::recursive_directory_iterator iterator(
            m_outputStaticParticlesRoot,
            std::filesystem::directory_options::skip_permission_denied,
            error
        );

        const std::filesystem::recursive_directory_iterator end;

        if (error) {

            std::printf(
                "[TextureMap2DWorkspace] Could not begin OUTPUT scan.\n"
            );

            return false;
        }

        while (iterator != end) {

            const std::filesystem::directory_entry entry =
                *iterator;

            std::error_code entryError;

            const bool regularFile =
                entry.is_regular_file(
                    entryError
                );

            if (!entryError &&
                regularFile &&
                isVspaManifestPath(entry.path())) {

                StaticParticleAsset manifestAsset;
                VspaLoadReport manifestReport;

                const bool valid =
                    loadVspaManifest(
                        entry.path(),
                        manifestAsset,
                        manifestReport
                    );

                StaticAssetCatalogEntry catalogEntry;

                catalogEntry.manifestPath =
                    entry.path().lexically_normal();

                catalogEntry.source =
                    "OUTPUT";

                catalogEntry.valid =
                    valid;

                if (!manifestAsset.name.empty()) {

                    catalogEntry.displayName =
                        manifestAsset.name;
                }
                else {

                    // foo.vspa.json
                    //   -> foo.vspa
                    //   -> foo
                    catalogEntry.displayName =
                        entry.path()
                        .stem()
                        .stem()
                        .string();
                }

                if (valid) {

                    catalogEntry.validationMessage =
                        "VALID";
                }
                else if (!manifestReport.errors.empty()) {

                    catalogEntry.validationMessage =
                        manifestReport.errors.front();
                }
                else {

                    catalogEntry.validationMessage =
                        "INVALID";
                }

                nextCatalog.push_back(
                    std::move(catalogEntry)
                );
            }

            iterator.increment(error);

            if (error) {

                std::printf(
                    "[TextureMap2DWorkspace] OUTPUT catalog scan failed "
                    "during directory traversal.\n"
                );

                return false;
            }
        }

        // ---------------------------------------------------------
        // Deterministic ordering:
        //
        //     display name, case-insensitive
        //     manifest path as tie-breaker
        // ---------------------------------------------------------
        std::sort(
            nextCatalog.begin(),
            nextCatalog.end(),
            [](const StaticAssetCatalogEntry& a,
                const StaticAssetCatalogEntry& b) {

                    const std::string nameA =
                        lowerAscii(a.displayName);

                    const std::string nameB =
                        lowerAscii(b.displayName);

                    if (nameA != nameB) {
                        return nameA < nameB;
                    }

                    return normalizedPathKey(
                        a.manifestPath
                    ) <
                        normalizedPathKey(
                            b.manifestPath
                        );
            }
        );

        std::size_t nextSelection = 0;
        bool restoredPreviousSelection = false;

        // ---------------------------------------------------------
        // Prefer preserving the manifest that was selected before
        // the refresh.
        // ---------------------------------------------------------
        if (!previousSelectionKey.empty()) {

            for (std::size_t index = 0;
                index < nextCatalog.size(); index++) {

                if (normalizedPathKey(
                    nextCatalog[index].manifestPath) ==
                    previousSelectionKey) {

                    nextSelection = index;

                    restoredPreviousSelection =
                        true;

                    break;
                }
            }
        }

        // ---------------------------------------------------------
        // If the old asset vanished, prefer the first VALID bundle.
        // If every entry is invalid, row zero remains selected so
        // the user can still inspect the invalid catalog entry.
        // ---------------------------------------------------------
        if (!restoredPreviousSelection) {

            for (std::size_t index = 0;
                index < nextCatalog.size();
                ++index) {

                if (nextCatalog[index].valid) {

                    nextSelection = index;
                    break;
                }
            }
        }

        m_outputCatalog =
            std::move(nextCatalog);

        m_selectedOutputIndex =
            m_outputCatalog.empty()
            ? 0
            : nextSelection;

        // A successful scan means the catalog is ready even when
        // OUTPUT/STATIC_PARTICLES contains zero assets.
        m_outputCatalogReady = true;

        std::printf(
            "[TextureMap2DWorkspace] OUTPUT catalog refreshed: "
            "%zu asset(s).\n",
            m_outputCatalog.size()
        );

        if (const StaticAssetCatalogEntry* selected =
            selectedOutputAsset()) {

            std::printf(
                "  Selected: %s [%s]\n",
                selected->displayName.c_str(),
                selected->valid
                ? "READY"
                : "INVALID"
            );
        }

        return true;
    }

    bool TextureMap2DWorkspace::selectOutputAsset(int direction) {

        if (direction == 0 ||
            m_outputCatalog.empty()) {

            return false;
        }

        const int count =
            static_cast<int>(
                m_outputCatalog.size()
                );

        const int current =
            static_cast<int>(
                m_selectedOutputIndex
                );

        const int step =
            direction < 0
            ? -1
            : +1;

        const int next =
            (current + step + count) %
            count;

        const bool changed =
            next != current;

        m_selectedOutputIndex =
            static_cast<std::size_t>(next);

        // ---------------------------------------------------------
        // Selecting a different OUTPUT asset invalidates the current
        // texture-map target.
        //
        // The previously loaded asset may remain in the shared
        // ProjectAssetRepository, but it is no longer the active
        // TEXTURE_MAP_2D target.
        // ---------------------------------------------------------
        if (changed) {

            m_target = TextureMapTargetContext{};
            m_session = TextureMapEditSession{};
        }

        return changed;
    }

    const StaticAssetCatalogEntry*
        TextureMap2DWorkspace::selectedOutputAsset() const {

        if (m_outputCatalog.empty() ||
            m_selectedOutputIndex >=
            m_outputCatalog.size()) {

            return nullptr;
        }

        return &m_outputCatalog[
            m_selectedOutputIndex
        ];
    }

    bool TextureMap2DWorkspace::activateLoadedTarget(
        AssetId assetId) {

        if (!m_repository) {

            m_target =
                TextureMapTargetContext{};

            m_target.readiness =
                TextureTargetReadiness::Invalid;

            return false;
        }

        const StaticParticleAsset* asset =
            m_repository->findStaticParticle(
                assetId
            );

        if (!asset) {

            m_target =
                TextureMapTargetContext{};

            m_target.readiness =
                TextureTargetReadiness::Invalid;

            return false;
        }

        // ---------------------------------------------------------
        // Establish a fresh Layer 2 target context.
        // ---------------------------------------------------------
        m_target =
            TextureMapTargetContext{};

        m_session =
            TextureMapEditSession{};

        m_target.assetId =
            assetId;

        m_target.loaded =
            true;

        // ---------------------------------------------------------
        // Basic mesh validation.
        // ---------------------------------------------------------
        if (asset->mesh.empty()) {

            m_target.readiness =
                TextureTargetReadiness::Invalid;

            return false;
        }

        // ---------------------------------------------------------
        // TEXCOORD_0 readiness.
        // ---------------------------------------------------------
        if (asset->mesh.uvs.size() !=
            asset->mesh.positions.size()) {

            m_target.readiness =
                TextureTargetReadiness::NeedsUv;

            return true;
        }

        // ---------------------------------------------------------
        // Determine the initial material slot.
        //
        // Prefer the first submesh's assigned material when valid.
        // Otherwise use material slot zero.
        // ---------------------------------------------------------
        if (asset->materials.empty()) {

            m_target.readiness =
                TextureTargetReadiness::NeedsTexture;

            return true;
        }

        std::size_t materialIndex = 0;

        if (!asset->submeshes.empty()) {

            const std::size_t candidate =
                static_cast<std::size_t>(
                    asset->submeshes.front().materialIndex
                    );

            if (candidate < asset->materials.size()) {

                materialIndex =
                    candidate;
            }
        }

        m_target.submeshIndex =
            0;

        m_target.materialIndex =
            materialIndex;

        m_target.channel =
            TextureUsage::BaseColor;

        const MaterialSlot& material =
            asset->materials[materialIndex];

        m_target.textureId =
            material.baseColorTextureId;

        // ---------------------------------------------------------
        // BASE_COLOR readiness.
        // ---------------------------------------------------------
        if (m_target.textureId.empty()) {

            m_target.readiness =
                TextureTargetReadiness::NeedsTexture;

            return true;
        }

        const TextureResource* texture =
            asset->findTexture(
                m_target.textureId
            );

        if (!texture ||
            !texture->valid) {

            m_target.readiness =
                TextureTargetReadiness::NeedsTexture;

            return true;
        }

        // ---------------------------------------------------------
        // Target is ready for TEXTURE_MAP_2D Layer 2.
        // ---------------------------------------------------------
        m_target.readiness =
            TextureTargetReadiness::Ready;

        return true;
    }

    bool TextureMap2DWorkspace::adjustPreviewParticleRadius(int direction) {

        if (!m_target.loaded ||
            direction == 0) {

            return false;
        }

        const float currentRadius =
            m_target.previewParticleRadius;

        std::size_t currentIndex = 0;

        float nearestDistance =
            std::fabs(
                currentRadius -
                kPreviewParticleRadiusPresets[0]
            );

        for (std::size_t i = 1;
            i < kPreviewParticleRadiusPresets.size();
            i++) {

            const float distance =
                std::fabs(
                    currentRadius -
                    kPreviewParticleRadiusPresets[i]
                );

            if (distance < nearestDistance) {

                nearestDistance = distance;
                currentIndex = i;
            }
        }

        std::size_t nextIndex = currentIndex;

        if (direction < 0) {

            if (currentIndex == 0) {
                return false;
            }

            nextIndex = currentIndex - 1;
        }
        else {

            if (currentIndex + 1 >=
                kPreviewParticleRadiusPresets.size()) {

                return false;
            }

            nextIndex = currentIndex + 1;
        }

        const float nextRadius =
            kPreviewParticleRadiusPresets[nextIndex];

        if (nextRadius == currentRadius) {

            return false;
        }

        m_target.previewParticleRadius = nextRadius;

        return true;
    }

    const std::array<TextureMapLineColorPreset, 4>&
        TextureMap2DWorkspace::lineColorPresets() {

        static const std::array<TextureMapLineColorPreset, 4> presets{
            TextureMapLineColorPreset{ "BLACK", { 12u, 12u, 14u, 255u } },
            TextureMapLineColorPreset{ "BROWN", { 96u, 55u, 32u, 255u } },
            TextureMapLineColorPreset{ "SILVER", { 184u, 192u, 204u, 255u } },
            TextureMapLineColorPreset{ "DARK_RED", { 92u, 10u, 18u, 255u } }
        };
        return presets;
    }

    const char* TextureMap2DWorkspace::boxAtlasFaceAxisName(
        std::uint32_t faceIndex) {

        // This table is the public editor contract for the existing
        // MeshUVGenerator::faceCell mapping.
        static const char* names[6]{
            "+X", "-X", "+Y", "-Y", "+Z", "-Z"
        };
        return faceIndex < 6u ? names[faceIndex] : "INVALID";
    }

    bool TextureMap2DWorkspace::channelActive(TextureMapChannel channel) {
        return channel == TextureMapChannel::BaseColor ||
            channel == TextureMapChannel::EmissiveColor;
    }

    bool TextureMap2DWorkspace::refreshTargetContext() {
        return m_target.assetId != INVALID_ASSET_ID &&
            activateLoadedTarget(m_target.assetId);
    }

    bool TextureMap2DWorkspace::beginAuthoringRuntime() {
        if (!m_initialized || !m_repository || !m_target.loaded ||
            m_target.readiness != TextureTargetReadiness::Ready ||
            !m_repository->findStaticParticle(m_target.assetId)) {
            m_runtimeStatusMessage = "TEXTURE MAP TARGET IS NOT READY.";
            return false;
        }

        m_subLayer = TextureMapSubLayer::CycleSetup;
        m_authoringMode = TextureMapAuthoringMode::Coloring;
        m_previewSource = TextureMapPreviewSource::Working;
        m_selectedChannel = TextureMapChannel::BaseColor;
        m_contourAction = TextureMapContourAction::New;
        m_selectedSurfaceTarget = -1;
        m_selectedContourTarget = -1;
        m_nestedFocus = false;
        m_baseMaterialSourceSelected = false;
        const StaticParticleAsset* canonical =
            m_repository->findStaticParticle(m_target.assetId);
        m_emissiveIntensity = canonical &&
            m_target.materialIndex < canonical->materials.size()
            ? canonical->materials[m_target.materialIndex].emissiveIntensity
            : 1.0f;
        m_runtimeStatusMessage.clear();
        m_session.clear();
        return true;
    }

    int TextureMap2DWorkspace::runtimeRowCount() const {
        switch (m_subLayer) {
        case TextureMapSubLayer::CycleSetup: return 3;
        case TextureMapSubLayer::BranchSetup: return 4;
        case TextureMapSubLayer::PixelEditor:
            if (m_authoringMode == TextureMapAuthoringMode::Coloring) return 7;
            return 5;
        case TextureMapSubLayer::CommitSave: return 5;
        default: return 0;
        }
    }

    bool TextureMap2DWorkspace::adjustRuntimeValue(int row, int direction) {
        if (direction == 0) return false;
        const int step = direction < 0 ? -1 : 1;
        StaticParticleAsset* asset = m_repository
            ? m_repository->findStaticParticle(m_target.assetId)
            : nullptr;

        if (m_subLayer == TextureMapSubLayer::CycleSetup) {
            if (row == 0) {
                m_authoringMode = static_cast<TextureMapAuthoringMode>(
                    wrappedIndex(static_cast<int>(m_authoringMode) + step, 3));
                return true;
            }
            if (row == 1) {
                m_previewSource = m_previewSource == TextureMapPreviewSource::Working
                    ? TextureMapPreviewSource::Committed
                    : TextureMapPreviewSource::Working;
                return true;
            }
            return false;
        }

        if (m_subLayer == TextureMapSubLayer::BranchSetup) {
            if (m_authoringMode == TextureMapAuthoringMode::Coloring) {
                if (row == 0 && asset) {
                    const int count = static_cast<int>(asset->surfaceTargets.size()) + 1;
                    m_selectedSurfaceTarget =
                        wrappedIndex(m_selectedSurfaceTarget + 1 + step, count) - 1;
                    return true;
                }
                if (row == 1) {
                    if (m_nestedFocus) {
                        if (m_selectedChannel == TextureMapChannel::BaseColor)
                            return selectBaseMaterial(step);
                        if (m_selectedChannel == TextureMapChannel::EmissiveColor) {
                            const float next = std::max(0.0f, std::min(4.0f,
                                m_emissiveIntensity + 0.25f * static_cast<float>(step)));
                            if (next == m_emissiveIntensity) return false;
                            m_emissiveIntensity = next;
                            return true;
                        }
                        return false;
                    }
                    m_selectedChannel = static_cast<TextureMapChannel>(
                        wrappedIndex(static_cast<int>(m_selectedChannel) + step,
                            static_cast<int>(TextureMapChannel::Count)));
                    return true;
                }
            }
            else if (m_authoringMode == TextureMapAuthoringMode::Contour) {
                if (row == 0) {
                    m_contourAction = m_contourAction == TextureMapContourAction::New
                        ? TextureMapContourAction::EditExisting
                        : TextureMapContourAction::New;
					if (m_contourAction == TextureMapContourAction::EditExisting &&
						asset && !asset->surfaceTargets.empty())
						m_selectedContourTarget = 0;
                    return true;
                }
                if (row == 1 && asset &&
                    m_contourAction == TextureMapContourAction::EditExisting &&
                    !asset->surfaceTargets.empty()) {
                    m_selectedContourTarget = wrappedIndex(
                        m_selectedContourTarget + step,
                        static_cast<int>(asset->surfaceTargets.size()));
                    return true;
                }
            }
            else if (row == 0 && asset) {
                const int count = static_cast<int>(asset->surfaceTargets.size()) + 1;
                m_selectedSurfaceTarget =
                    wrappedIndex(m_selectedSurfaceTarget + 1 + step, count) - 1;
                return true;
            }
            return false;
        }

        if (m_subLayer != TextureMapSubLayer::PixelEditor) return false;

        if (row == 0) {
            if (m_authoringMode == TextureMapAuthoringMode::Contour &&
                !m_session.contourCells.empty()) {
                m_runtimeStatusMessage = "SELECT FACE IS LOCKED WHILE CONTOUR HAS POINTS.";
                return false;
            }
            m_session.selectedFace = static_cast<std::uint32_t>(
                wrappedIndex(static_cast<int>(m_session.selectedFace) + step, 6));
            return true;
        }

        if (m_authoringMode == TextureMapAuthoringMode::Coloring &&
            row >= 1 && row <= 4) {
            const std::size_t component = static_cast<std::size_t>(row - 1);
            const int value = static_cast<int>(m_session.paintColor[component]) + step;
            const std::uint8_t next = static_cast<std::uint8_t>(
                std::max(0, std::min(255, value)));
            if (next == m_session.paintColor[component]) return false;
            m_session.paintColor[component] = next;
            return true;
        }

        if (m_authoringMode == TextureMapAuthoringMode::PanelLines) {
            if (row == 1) {
                const int next = std::max(1, std::min(8,
                    m_session.lineThickness + step));
                if (next == m_session.lineThickness) return false;
                m_session.lineThickness = next;
                return true;
            }
            if (row == 2) {
                m_session.lineColorPreset = static_cast<std::size_t>(wrappedIndex(
                    static_cast<int>(m_session.lineColorPreset) + step,
                    static_cast<int>(lineColorPresets().size())));
                return true;
            }
        }
        return false;
    }

    TextureMap2DWorkspaceAction TextureMap2DWorkspace::activateRuntimeRow(
        int row,
        std::string* diagnostic) {

        auto reject = [&](const std::string& message) {
            m_runtimeStatusMessage = message;
            if (diagnostic) *diagnostic = message;
            return TextureMap2DWorkspaceAction::Rejected;
        };

        m_runtimeStatusMessage.clear();

        if (m_subLayer == TextureMapSubLayer::CycleSetup) {
            if (row != 2) return TextureMap2DWorkspaceAction::None;
            m_subLayer = TextureMapSubLayer::BranchSetup;
            m_nestedFocus = false;
            return TextureMap2DWorkspaceAction::StateChanged;
        }

        if (m_subLayer == TextureMapSubLayer::BranchSetup) {
            if (row == 1 && m_authoringMode == TextureMapAuthoringMode::Coloring) {
                if (!channelActive(m_selectedChannel))
                    return reject("SELECTED TEXTURE CHANNEL IS INACTIVE.");
                m_nestedFocus = !m_nestedFocus;
                return TextureMap2DWorkspaceAction::StateChanged;
            }

            if (row == 2) {
                if (m_authoringMode == TextureMapAuthoringMode::Coloring &&
                    !channelActive(m_selectedChannel))
                    return reject("SELECTED TEXTURE CHANNEL IS INACTIVE.");
                const StaticParticleAsset* asset = m_repository
                    ? m_repository->findStaticParticle(m_target.assetId)
                    : nullptr;
                if (m_authoringMode == TextureMapAuthoringMode::Contour &&
                    m_contourAction == TextureMapContourAction::EditExisting &&
                    (!asset || asset->surfaceTargets.empty()))
                    return reject("NO EXISTING SURFACE TARGETS ARE AVAILABLE.");
                if (!prepareWorkingPass(diagnostic))
                    return reject(diagnostic && !diagnostic->empty()
                        ? *diagnostic : "WORKING PASS COULD NOT BE PREPARED.");
                m_subLayer = TextureMapSubLayer::PixelEditor;
                m_nestedFocus = false;
                return TextureMap2DWorkspaceAction::StateChanged;
            }

            if (row == 3) {
                abandonWorkingPass();
                m_subLayer = TextureMapSubLayer::CycleSetup;
                m_nestedFocus = false;
                return TextureMap2DWorkspaceAction::StateChanged;
            }
            return TextureMap2DWorkspaceAction::None;
        }

        if (m_subLayer == TextureMapSubLayer::PixelEditor) {
            if (m_authoringMode == TextureMapAuthoringMode::Coloring) {
                if (row == 5) {
                    m_subLayer = TextureMapSubLayer::CommitSave;
                    return TextureMap2DWorkspaceAction::StateChanged;
                }
                if (row == 6) {
                    abandonWorkingPass();
                    m_subLayer = TextureMapSubLayer::BranchSetup;
                    return TextureMap2DWorkspaceAction::StateChanged;
                }
            }
            else if (m_authoringMode == TextureMapAuthoringMode::Contour) {
                if (row == 1) {
                    return closeContour(diagnostic)
                        ? TextureMap2DWorkspaceAction::StateChanged
                        : TextureMap2DWorkspaceAction::Rejected;
                }
                if (row == 2) {
                    return undoContourPoint()
                        ? TextureMap2DWorkspaceAction::StateChanged
                        : TextureMap2DWorkspaceAction::Rejected;
                }
                if (row == 3) {
                    if (!m_session.contourClosed || contourHasSelfIntersection())
                        return reject("CLOSE A VALID CONTOUR BEFORE REVIEW / COMMIT.");
                    m_subLayer = TextureMapSubLayer::CommitSave;
                    return TextureMap2DWorkspaceAction::StateChanged;
                }
                if (row == 4) {
                    abandonWorkingPass();
                    m_subLayer = TextureMapSubLayer::BranchSetup;
                    return TextureMap2DWorkspaceAction::StateChanged;
                }
            }
            else {
                if (row == 3) {
                    m_subLayer = TextureMapSubLayer::CommitSave;
                    return TextureMap2DWorkspaceAction::StateChanged;
                }
                if (row == 4) {
                    abandonWorkingPass();
                    m_subLayer = TextureMapSubLayer::BranchSetup;
                    return TextureMap2DWorkspaceAction::StateChanged;
                }
            }
            return TextureMap2DWorkspaceAction::None;
        }

        if (m_subLayer == TextureMapSubLayer::CommitSave) {
            if (row == 0) {
                if (!m_session.dirty) return reject("WORKING EDIT IS ALREADY CLEAN.");
                if (m_authoringMode == TextureMapAuthoringMode::Contour &&
                    m_contourAction == TextureMapContourAction::New)
                    return TextureMap2DWorkspaceAction::RequestSurfaceTargetName;
                return commitWorkingEdit(std::string{}, diagnostic)
                    ? TextureMap2DWorkspaceAction::StateChanged
                    : TextureMap2DWorkspaceAction::Rejected;
            }
            if (row == 1) {
                if (m_session.dirty)
                    return reject("COMMIT WORKING EDIT TO TARGET FIRST.");
                return TextureMap2DWorkspaceAction::RequestSaveCurrent;
            }
            if (row == 2) return TextureMap2DWorkspaceAction::RequestSaveAs;
            if (row == 3) {
                m_subLayer = TextureMapSubLayer::PixelEditor;
                return TextureMap2DWorkspaceAction::StateChanged;
            }
            if (row == 4) {
                abandonWorkingPass();
                m_subLayer = TextureMapSubLayer::CycleSetup;
                return TextureMap2DWorkspaceAction::StateChanged;
            }
        }
        return TextureMap2DWorkspaceAction::None;
    }

    ImageRGBA8 TextureMap2DWorkspace::textureImage(
        const TextureResource* texture) const {

        ImageRGBA8 image;
        if (!texture) return image;
        if (texture->width > 0u && texture->height > 0u &&
            texture->channels == 4u &&
            texture->pixels.size() == static_cast<std::size_t>(texture->width) *
                texture->height * 4u) {
            image.width = texture->width;
            image.height = texture->height;
            image.pixels = texture->pixels;
            return image;
        }
        if (!texture->sourcePath.empty()) {
            std::string ignored;
            loadPngImage(texture->sourcePath, image, &ignored, true);
        }
        return image;
    }

    bool TextureMap2DWorkspace::prepareWorkingPass(std::string* diagnostic) {
        StaticParticleAsset* asset = m_repository
            ? m_repository->findStaticParticle(m_target.assetId)
            : nullptr;
        if (!asset || m_target.materialIndex >= asset->materials.size()) {
            if (diagnostic) *diagnostic = "CANONICAL TARGET MATERIAL IS UNAVAILABLE.";
            return false;
        }

        TextureMapEditSession session;
        session.active = true;
        session.assetId = asset->id;
        session.materialIndex = m_target.materialIndex;
        session.submeshIndex = m_target.submeshIndex;
        session.authoringMode = m_authoringMode;
        session.channel = m_selectedChannel;
        session.contourAction = m_contourAction;
        session.selectedSurfaceTarget = m_selectedSurfaceTarget;
        session.selectedContourTarget = m_selectedContourTarget;
        session.viewMode = TextureMapViewMode::Edit;
        session.emissiveIntensity = m_emissiveIntensity;

        const MaterialSlot& material = asset->materials[m_target.materialIndex];
        const TextureResource* base = asset->findTexture(material.baseColorTextureId);
        ImageRGBA8 baseImage = textureImage(base);
        if (!baseImage.valid()) {
            if (diagnostic) *diagnostic = "BASE COLOR TEXTURE DATA IS UNAVAILABLE.";
            return false;
        }

        session.originalImage = baseImage;
        session.baseMaterialImage = baseImage;
        session.compositeImage = baseImage;
        session.workingImage = baseImage;
        session.textureId = material.baseColorTextureId;

        if (m_authoringMode == TextureMapAuthoringMode::Coloring) {
            if (m_selectedChannel == TextureMapChannel::BaseColor) {
                const BaseMaterialCatalogEntry* source = selectedBaseMaterial();
                if (m_baseMaterialSourceSelected && source && source->valid) {
                    ImageRGBA8 selected;
                    std::string error;
                    if (loadPngImage(source->baseColorPath, selected, &error, true) &&
                        selected.width == baseImage.width && selected.height == baseImage.height) {
                        session.workingImage = selected;
                        session.baseMaterialImage = selected;
                        session.dirty = selected.pixels != baseImage.pixels;
                    }
                }
            }
            else if (m_selectedChannel == TextureMapChannel::EmissiveColor) {
                const TextureResource* emissive = asset->findTexture(material.emissiveTextureId);
                ImageRGBA8 emissiveImage = textureImage(emissive);
                if (!emissiveImage.valid())
                    emissiveImage = makeSolidImage(baseImage.width, baseImage.height, 0u, 0u, 0u, 0u);
                session.originalImage = emissiveImage;
                session.workingImage = emissiveImage;
                session.textureId = material.emissiveTextureId;
				session.dirty = std::fabs(
					session.emissiveIntensity - material.emissiveIntensity) > 0.0001f;
            }
        }
        else if (m_authoringMode == TextureMapAuthoringMode::Contour) {
            if (m_contourAction == TextureMapContourAction::EditExisting) {
                if (asset->surfaceTargets.empty()) {
                    if (diagnostic) *diagnostic = "NO EXISTING SURFACE TARGETS ARE AVAILABLE.";
                    return false;
                }
                const int targetIndex = wrappedIndex(m_selectedContourTarget,
                    static_cast<int>(asset->surfaceTargets.size()));
                session.selectedContourTarget = targetIndex;
                const SurfaceTarget& target = asset->surfaceTargets[
                    static_cast<std::size_t>(targetIndex)];
                session.selectedFace = target.faceIndex;
                const float grid = static_cast<float>(m_target.pixelGridDivisions);
                for (const Vec2& point : target.normalizedPolygon) {
                    session.contourCells.push_back({
                        static_cast<int>(std::lround(point.x * grid - 0.5f)),
                        static_cast<int>(std::lround(point.y * grid - 0.5f))
                    });
                }
                session.contourClosed = true;
            }
        }

        session.revision = 1u;
        m_session = std::move(session);
        return true;
    }

    bool TextureMap2DWorkspace::beginEditSession() {
        return prepareWorkingPass(nullptr);
    }

    bool TextureMap2DWorkspace::abandonWorkingPass() {
        const bool hadSession = m_session.active;
        m_session.clear();
        return hadSession;
    }

    bool TextureMap2DWorkspace::cancelEditSession() {
        return abandonWorkingPass();
    }

    bool TextureMap2DWorkspace::cellInsideSelectedSurfaceTarget(
        const TextureMapGridCell& cell) const {

        if (m_selectedSurfaceTarget < 0) return true;
        const StaticParticleAsset* asset = m_repository
            ? m_repository->findStaticParticle(m_target.assetId)
            : nullptr;
        if (!asset || static_cast<std::size_t>(m_selectedSurfaceTarget) >=
            asset->surfaceTargets.size()) return false;
        const SurfaceTarget& target = asset->surfaceTargets[
            static_cast<std::size_t>(m_selectedSurfaceTarget)];
        if (target.faceIndex != m_session.selectedFace) return false;
        const float grid = static_cast<float>(m_target.pixelGridDivisions);
        return pointInPolygon(
            (static_cast<float>(cell.x) + 0.5f) / grid,
            (static_cast<float>(cell.y) + 0.5f) / grid,
            target.normalizedPolygon);
    }

    bool TextureMap2DWorkspace::applyCell(
        const TextureMapGridCell& cell,
        bool panelLine) {

        if (!m_session.active || !m_session.workingImage.valid() ||
            cell.x < 0 || cell.y < 0 ||
            cell.x >= static_cast<int>(m_target.pixelGridDivisions) ||
            cell.y >= static_cast<int>(m_target.pixelGridDivisions) ||
            !cellInsideSelectedSurfaceTarget(cell)) return false;

        const int divisions = static_cast<int>(m_target.pixelGridDivisions);
        const int column = static_cast<int>(m_session.selectedFace % 3u);
        const int row = static_cast<int>(m_session.selectedFace / 3u);
        const int halfThickness = panelLine ? (m_session.lineThickness - 1) / 2 : 0;
        const int extraThickness = panelLine ? m_session.lineThickness / 2 : 0;
        const std::array<std::uint8_t, 4> color = panelLine
            ? lineColorPresets()[m_session.lineColorPreset].rgba
            : m_session.paintColor;
        bool changed = false;

        for (int oy = -halfThickness; oy <= extraThickness; oy++) {
            for (int ox = -halfThickness; ox <= extraThickness; ox++) {
                const TextureMapGridCell expanded{ cell.x + ox, cell.y + oy };
                if (expanded.x < 0 || expanded.y < 0 ||
                    expanded.x >= divisions || expanded.y >= divisions ||
                    !cellInsideSelectedSurfaceTarget(expanded)) continue;

                const float localX0 = static_cast<float>(expanded.x) /
                    static_cast<float>(divisions);
                const float localX1 = static_cast<float>(expanded.x + 1) /
                    static_cast<float>(divisions);
                const float localY0 = static_cast<float>(expanded.y) /
                    static_cast<float>(divisions);
                const float localY1 = static_cast<float>(expanded.y + 1) /
                    static_cast<float>(divisions);
                const int pixelX0 = static_cast<int>(std::floor(
                    (static_cast<float>(column) + localX0) / 3.0f *
                    static_cast<float>(m_session.workingImage.width)));
                const int pixelX1 = static_cast<int>(std::ceil(
                    (static_cast<float>(column) + localX1) / 3.0f *
                    static_cast<float>(m_session.workingImage.width))) - 1;
                const int pixelY0 = static_cast<int>(std::floor(
                    (static_cast<float>(row) + localY0) / 2.0f *
                    static_cast<float>(m_session.workingImage.height)));
                const int pixelY1 = static_cast<int>(std::ceil(
                    (static_cast<float>(row) + localY1) / 2.0f *
                    static_cast<float>(m_session.workingImage.height))) - 1;

                for (int py = pixelY0; py <= pixelY1; py++) {
                    for (int px = pixelX0; px <= pixelX1; px++) {
                        if (px < 0 || py < 0 ||
                            px >= static_cast<int>(m_session.workingImage.width) ||
                            py >= static_cast<int>(m_session.workingImage.height)) continue;
                        const std::size_t offset =
                            (static_cast<std::size_t>(py) * m_session.workingImage.width +
                                static_cast<std::size_t>(px)) * 4u;
                        for (std::size_t component = 0; component < 4u; component++) {
                            if (m_session.workingImage.pixels[offset + component] != color[component]) {
                                m_session.workingImage.pixels[offset + component] = color[component];
                                changed = true;
                            }
                        }
                    }
                }
            }
        }

        if (changed) {
            m_session.dirty = true;
            m_session.revision++;
        }
        return changed;
    }

    bool TextureMap2DWorkspace::rasterizeStroke(
        const TextureMapGridCell& from,
        const TextureMapGridCell& to,
        bool panelLine) {

        int x0 = from.x;
        int y0 = from.y;
        const int x1 = to.x;
        const int y1 = to.y;
        const int dx = std::abs(x1 - x0);
        const int sx = x0 < x1 ? 1 : -1;
        const int dy = -std::abs(y1 - y0);
        const int sy = y0 < y1 ? 1 : -1;
        int error = dx + dy;
        bool changed = false;

        for (;;) {
            changed = applyCell({ x0, y0 }, panelLine) || changed;
            if (x0 == x1 && y0 == y1) break;
            const int twiceError = error * 2;
            if (twiceError >= dy) { error += dy; x0 += sx; }
            if (twiceError <= dx) { error += dx; y0 += sy; }
        }
        return changed;
    }

    bool TextureMap2DWorkspace::setCursorCell(int x, int y) {
        const int divisions = static_cast<int>(m_target.pixelGridDivisions);
        TextureMapGridCell next;
        if (x >= 0 && y >= 0 && x < divisions && y < divisions)
            next = { x, y };
        if (next == m_session.cursorCell) return false;
        m_session.cursorCell = next;
        return true;
    }

    bool TextureMap2DWorkspace::beginAuthoringStroke(int x, int y) {
        if (m_subLayer != TextureMapSubLayer::PixelEditor ||
            m_session.viewMode != TextureMapViewMode::Edit) return false;
        if (m_authoringMode == TextureMapAuthoringMode::Contour)
            return addContourPoint(x, y);
        const TextureMapGridCell cell{ x, y };
        if (!cell.valid()) return false;
        m_session.strokeActive = true;
        m_session.previousStrokeCell = cell;
        return applyCell(cell,
            m_authoringMode == TextureMapAuthoringMode::PanelLines);
    }

    bool TextureMap2DWorkspace::continueAuthoringStroke(int x, int y) {
        if (!m_session.strokeActive ||
            m_authoringMode == TextureMapAuthoringMode::Contour) return false;
        const TextureMapGridCell cell{ x, y };
        if (!cell.valid() || cell == m_session.previousStrokeCell) return false;
        const bool changed = rasterizeStroke(
            m_session.previousStrokeCell,
            cell,
            m_authoringMode == TextureMapAuthoringMode::PanelLines);
        m_session.previousStrokeCell = cell;
        return changed;
    }

    bool TextureMap2DWorkspace::endAuthoringStroke() {
        const bool active = m_session.strokeActive;
        m_session.strokeActive = false;
        m_session.previousStrokeCell = TextureMapGridCell{};
        return active;
    }

    bool TextureMap2DWorkspace::addContourPoint(int x, int y) {
        if (!m_session.active || m_authoringMode != TextureMapAuthoringMode::Contour ||
            m_session.viewMode != TextureMapViewMode::Edit ||
            m_session.contourClosed) return false;
        const int divisions = static_cast<int>(m_target.pixelGridDivisions);
        const TextureMapGridCell cell{ x, y };
        if (x < 0 || y < 0 || x >= divisions || y >= divisions) return false;
        for (const TextureMapGridCell& existing : m_session.contourCells)
            if (existing == cell) return false;
        m_session.contourCells.push_back(cell);
        m_session.dirty = true;
        m_session.revision++;
        return true;
    }

    bool TextureMap2DWorkspace::contourHasSelfIntersection() const {
        const std::size_t count = m_session.contourCells.size();
        if (count < 4u) return false;
        for (std::size_t i = 0; i < count; i++) {
            const std::size_t iNext = (i + 1u) % count;
            for (std::size_t j = i + 1u; j < count; j++) {
                const std::size_t jNext = (j + 1u) % count;
                if (i == j || iNext == j || jNext == i) continue;
                if (strictSegmentsIntersect(
                    m_session.contourCells[i], m_session.contourCells[iNext],
                    m_session.contourCells[j], m_session.contourCells[jNext]))
                    return true;
            }
        }
        return false;
    }

    bool TextureMap2DWorkspace::closeContour(std::string* diagnostic) {
        if (m_session.contourCells.size() < 3u) {
            m_runtimeStatusMessage = "CONTOUR REQUIRES AT LEAST 3 UNIQUE POINTS.";
            if (diagnostic) *diagnostic = m_runtimeStatusMessage;
            return false;
        }
        if (contourHasSelfIntersection()) {
            m_runtimeStatusMessage = "CONTOUR SELF-INTERSECTION IS NOT ALLOWED.";
            if (diagnostic) *diagnostic = m_runtimeStatusMessage;
            return false;
        }
        m_session.contourClosed = true;
        m_session.dirty = true;
        m_session.revision++;
        return true;
    }

    bool TextureMap2DWorkspace::undoContourPoint() {
        if (m_session.contourCells.empty()) return false;
        m_session.contourClosed = false;
        m_session.contourCells.pop_back();
        m_session.dirty = true;
        m_session.revision++;
        return true;
    }

    bool TextureMap2DWorkspace::toggleRuntimeView() {
        if (m_subLayer != TextureMapSubLayer::PixelEditor) return false;
        endAuthoringStroke();
        m_session.viewMode = m_session.viewMode == TextureMapViewMode::Edit
            ? TextureMapViewMode::Preview
            : TextureMapViewMode::Edit;
        return true;
    }

	bool TextureMap2DWorkspace::adjustEditorZoom(int direction) {
		if (m_subLayer != TextureMapSubLayer::PixelEditor || direction == 0)
			return false;
		const float next = std::max(0.65f, std::min(1.65f,
			m_session.editorZoom + (direction < 0 ? -0.10f : 0.10f)));
		if (next == m_session.editorZoom) return false;
		m_session.editorZoom = next;
		return true;
	}

    bool TextureMap2DWorkspace::canExitLayer3(std::string* diagnostic) const {
        if (!m_session.dirty) return true;
        if (diagnostic)
            *diagnostic = "WORKING EDIT IS DIRTY. COMMIT OR RETURN TO AUTHORING CYCLE SETUP.";
        return false;
    }

    TextureResource* TextureMap2DWorkspace::ensureEditableTexture(
        StaticParticleAsset& asset,
        TextureMapChannel channel,
        const ImageRGBA8& image) const {

        if (m_target.materialIndex >= asset.materials.size() || !image.valid())
            return nullptr;
        MaterialSlot& material = asset.materials[m_target.materialIndex];
        std::string* materialTextureId = channel == TextureMapChannel::EmissiveColor
            ? &material.emissiveTextureId
            : &material.baseColorTextureId;
        TextureResource* texture = asset.findTexture(*materialTextureId);
        if (!texture) {
            std::string stem = asset.name;
            for (char& c : stem) {
                const unsigned char uc = static_cast<unsigned char>(c);
                if (!std::isalnum(uc) && c != '_' && c != '-') c = '_';
            }
            const bool emissive = channel == TextureMapChannel::EmissiveColor;
            TextureResource created;
            created.id = "tex_" + stem + (emissive ? "_emissive" : "_basecolor");
            created.relativePath = "textures/" + stem +
                (emissive ? "_emissive.png" : "_basecolor.png");
            created.type = TextureType::Texture2D;
            created.usage = emissive ? TextureUsage::Emissive : TextureUsage::BaseColor;
            created.colorSpace = TextureColorSpace::SRGB;
            asset.textures.push_back(std::move(created));
            *materialTextureId = asset.textures.back().id;
            texture = &asset.textures.back();
        }
        texture->width = image.width;
        texture->height = image.height;
        texture->channels = 4u;
        texture->pixels = image.pixels;
        texture->loaded = true;
        texture->valid = true;
        texture->renderingDeferred = false;
        texture->sourcePath.clear();
        return texture;
    }

    bool TextureMap2DWorkspace::commitWorkingEdit(
        const std::string& newSurfaceTargetName,
        std::string* diagnostic) {

        StaticParticleAsset* asset = m_repository
            ? m_repository->findStaticParticle(m_target.assetId)
            : nullptr;
        if (!asset || !m_session.active || !m_session.dirty) {
            if (diagnostic) *diagnostic = "NO DIRTY WORKING EDIT IS AVAILABLE.";
            return false;
        }

        if (m_authoringMode == TextureMapAuthoringMode::Contour) {
            if (!m_session.contourClosed || contourHasSelfIntersection()) {
                if (diagnostic) *diagnostic = "WORKING CONTOUR IS NOT CLOSED AND VALID.";
                return false;
            }
            SurfaceTarget target;
            target.faceIndex = m_session.selectedFace;
            const float divisions = static_cast<float>(m_target.pixelGridDivisions);
            for (const TextureMapGridCell& cell : m_session.contourCells) {
                target.normalizedPolygon.push_back({
                    (static_cast<float>(cell.x) + 0.5f) / divisions,
                    (static_cast<float>(cell.y) + 0.5f) / divisions
                });
            }

            if (m_contourAction == TextureMapContourAction::New) {
                if (!validAuthoringName(newSurfaceTargetName)) {
                    if (diagnostic)
                        *diagnostic = "SURFACE TARGET NAME MAY USE LETTERS, NUMBERS, '_' OR '-'.";
                    return false;
                }
                const std::string key = lowerAscii(newSurfaceTargetName);
                for (const SurfaceTarget& existing : asset->surfaceTargets) {
                    if (lowerAscii(existing.name) == key) {
                        if (diagnostic) *diagnostic = "SURFACE TARGET NAME ALREADY EXISTS.";
                        return false;
                    }
                }
                target.name = newSurfaceTargetName;
                asset->surfaceTargets.push_back(std::move(target));
                m_selectedContourTarget =
                    static_cast<int>(asset->surfaceTargets.size()) - 1;
            }
            else {
                if (m_session.selectedContourTarget < 0 ||
                    static_cast<std::size_t>(m_session.selectedContourTarget) >=
                    asset->surfaceTargets.size()) {
                    if (diagnostic) *diagnostic = "EDIT CONTOUR TARGET IS UNAVAILABLE.";
                    return false;
                }
                target.name = asset->surfaceTargets[
                    static_cast<std::size_t>(m_session.selectedContourTarget)].name;
                asset->surfaceTargets[
                    static_cast<std::size_t>(m_session.selectedContourTarget)] =
                    std::move(target);
            }
        }
        else {
            const TextureMapChannel commitChannel =
                m_authoringMode == TextureMapAuthoringMode::PanelLines
                ? TextureMapChannel::BaseColor
                : m_selectedChannel;
            TextureResource* texture = ensureEditableTexture(
                *asset, commitChannel, m_session.workingImage);
            if (!texture) {
                if (diagnostic) *diagnostic = "WORKING TEXTURE COULD NOT BE COMMITTED.";
                return false;
            }
            m_session.textureId = texture->id;
            if (commitChannel == TextureMapChannel::EmissiveColor &&
                m_target.materialIndex < asset->materials.size()) {
                MaterialSlot& material = asset->materials[m_target.materialIndex];
                material.emissiveIntensity = m_session.emissiveIntensity;
                material.emissiveFactor[0] = 1.0f;
                material.emissiveFactor[1] = 1.0f;
                material.emissiveFactor[2] = 1.0f;
            }
        }

        asset->assetRevision++;
        m_session.dirty = false;
        m_session.originalImage = m_session.workingImage;
		m_emissiveIntensity = m_session.emissiveIntensity;
        m_session.revision++;
        m_runtimeStatusMessage = "WORKING EDIT COMMITTED TO IN-MEMORY TARGET.";
        return true;
    }

    bool TextureMap2DWorkspace::completeSurfaceTargetName(
        const std::string& name,
        std::string* diagnostic) {

        return commitWorkingEdit(name, diagnostic);
    }

    bool TextureMap2DWorkspace::validateNewSurfaceTargetName(
        const std::string& name,
        std::string* diagnostic) const {

        if (!validAuthoringName(name)) {
            if (diagnostic)
                *diagnostic = "SURFACE TARGET NAME MAY USE LETTERS, NUMBERS, '_' OR '-'.";
            return false;
        }
        const StaticParticleAsset* asset = m_repository
            ? m_repository->findStaticParticle(m_target.assetId)
            : nullptr;
        if (!asset) {
            if (diagnostic) *diagnostic = "CANONICAL TARGET IS UNAVAILABLE.";
            return false;
        }
        const std::string key = lowerAscii(name);
        for (const SurfaceTarget& target : asset->surfaceTargets) {
            if (lowerAscii(target.name) == key) {
                if (diagnostic) *diagnostic = "SURFACE TARGET NAME ALREADY EXISTS.";
                return false;
            }
        }
        return true;
    }

    bool TextureMap2DWorkspace::applyEditSessionToAsset() {
        return commitWorkingEdit(std::string{}, nullptr);
    }

    StaticParticleAsset TextureMap2DWorkspace::buildPreviewAsset() const {
        const StaticParticleAsset* canonical = m_repository
            ? m_repository->findStaticParticle(m_target.assetId)
            : nullptr;
        if (!canonical) return StaticParticleAsset{};
        StaticParticleAsset preview = *canonical;
        if (!m_session.active || !previewUsesWorkingState() ||
            m_authoringMode == TextureMapAuthoringMode::Contour ||
            !m_session.workingImage.valid() ||
            m_target.materialIndex >= preview.materials.size()) return preview;

        const TextureMapChannel channel =
            m_authoringMode == TextureMapAuthoringMode::PanelLines
            ? TextureMapChannel::BaseColor
            : m_selectedChannel;
        MaterialSlot& material = preview.materials[m_target.materialIndex];
        std::string* id = channel == TextureMapChannel::EmissiveColor
            ? &material.emissiveTextureId
            : &material.baseColorTextureId;
        TextureResource* texture = preview.findTexture(*id);
        if (!texture) {
            TextureResource created;
            created.id = channel == TextureMapChannel::EmissiveColor
                ? "__tm_working_emissive"
                : "__tm_working_basecolor";
            created.relativePath = channel == TextureMapChannel::EmissiveColor
                ? "textures/working_emissive.png"
                : "textures/working_basecolor.png";
            created.usage = channel == TextureMapChannel::EmissiveColor
                ? TextureUsage::Emissive
                : TextureUsage::BaseColor;
            created.colorSpace = TextureColorSpace::SRGB;
            preview.textures.push_back(std::move(created));
            *id = preview.textures.back().id;
            texture = &preview.textures.back();
        }
        texture->width = m_session.workingImage.width;
        texture->height = m_session.workingImage.height;
        texture->channels = 4u;
        texture->pixels = m_session.workingImage.pixels;
        texture->loaded = true;
        texture->valid = true;
        texture->sourcePath.clear();
        if (channel == TextureMapChannel::EmissiveColor) {
            material.emissiveIntensity = m_session.emissiveIntensity;
            material.emissiveFactor[0] = 1.0f;
            material.emissiveFactor[1] = 1.0f;
            material.emissiveFactor[2] = 1.0f;
        }
        return preview;
    }

    bool TextureMap2DWorkspace::buildSaveAsSnapshot(
        const std::string& assetName,
        StaticParticleAsset& output,
        std::string* diagnostic,
        const std::string& uncommittedSurfaceTargetName) const {

        if (!validAuthoringName(assetName)) {
            if (diagnostic)
                *diagnostic = "STATIC PARTICLE NAME MAY USE LETTERS, NUMBERS, '_' OR '-'.";
            return false;
        }
        const StaticParticleAsset* canonical = m_repository
            ? m_repository->findStaticParticle(m_target.assetId)
            : nullptr;
        if (!canonical || canonical->mesh.empty()) {
            if (diagnostic) *diagnostic = "CANONICAL TARGET IS UNAVAILABLE.";
            return false;
        }
        output = *canonical;
        if (m_session.dirty &&
            m_authoringMode != TextureMapAuthoringMode::Contour) {
            const TextureMapChannel channel =
                m_authoringMode == TextureMapAuthoringMode::PanelLines
                ? TextureMapChannel::BaseColor
                : m_selectedChannel;
            TextureResource* texture = ensureEditableTexture(
                output, channel, m_session.workingImage);
            if (!texture) {
                if (diagnostic) *diagnostic =
                    "WORKING TEXTURE COULD NOT BE INCLUDED IN SAVE AS.";
                return false;
            }
            if (channel == TextureMapChannel::EmissiveColor &&
                m_target.materialIndex < output.materials.size()) {
                MaterialSlot& material = output.materials[m_target.materialIndex];
                material.emissiveIntensity = m_session.emissiveIntensity;
                material.emissiveFactor[0] = 1.0f;
                material.emissiveFactor[1] = 1.0f;
                material.emissiveFactor[2] = 1.0f;
            }
        }
        if (m_session.dirty && m_authoringMode == TextureMapAuthoringMode::Contour) {
            if (!m_session.contourClosed || contourHasSelfIntersection()) {
                if (diagnostic) *diagnostic = "WORKING CONTOUR IS NOT CLOSED AND VALID.";
                return false;
            }
            SurfaceTarget target;
            target.faceIndex = m_session.selectedFace;
            const float divisions = static_cast<float>(m_target.pixelGridDivisions);
            for (const TextureMapGridCell& cell : m_session.contourCells) {
                target.normalizedPolygon.push_back({
                    (static_cast<float>(cell.x) + 0.5f) / divisions,
                    (static_cast<float>(cell.y) + 0.5f) / divisions
                });
            }
            if (m_contourAction == TextureMapContourAction::New) {
                if (!validateNewSurfaceTargetName(
                    uncommittedSurfaceTargetName, diagnostic)) return false;
                target.name = uncommittedSurfaceTargetName;
                output.surfaceTargets.push_back(std::move(target));
            }
            else if (m_session.selectedContourTarget >= 0 &&
                static_cast<std::size_t>(m_session.selectedContourTarget) <
                output.surfaceTargets.size()) {
                target.name = output.surfaceTargets[
                    static_cast<std::size_t>(m_session.selectedContourTarget)].name;
                output.surfaceTargets[
                    static_cast<std::size_t>(m_session.selectedContourTarget)] =
                    std::move(target);
            }
            else {
                if (diagnostic) *diagnostic = "EDIT CONTOUR TARGET IS UNAVAILABLE.";
                return false;
            }
        }
        output.id = INVALID_ASSET_ID;
        output.name = assetName;
        output.assetRevision++;
        return true;
    }

    bool TextureMap2DWorkspace::adoptSavedTarget(AssetId assetId) {
        if (!activateLoadedTarget(assetId)) return false;
        return beginAuthoringRuntime();
    }

    void TextureMap2DWorkspace::reset() {
        m_target = TextureMapTargetContext{};
        m_session.clear();
        m_focus = TextureMapFocus::TextureCanvas;
        m_subLayer = TextureMapSubLayer::CycleSetup;
        m_authoringMode = TextureMapAuthoringMode::Coloring;
        m_previewSource = TextureMapPreviewSource::Working;
        m_selectedChannel = TextureMapChannel::BaseColor;
        m_contourAction = TextureMapContourAction::New;
        m_selectedSurfaceTarget = -1;
        m_selectedContourTarget = -1;
        m_nestedFocus = false;
        m_baseMaterialSourceSelected = false;
        m_emissiveIntensity = 1.0f;
        m_runtimeStatusMessage.clear();
    }

    bool TextureMap2DWorkspace::selectBaseMaterial(int direction) {
        if (direction == 0 || m_baseMaterialCatalog.empty()) return false;
        const bool newlySelected = !m_baseMaterialSourceSelected;
        m_baseMaterialSourceSelected = true;
        const int count = static_cast<int>(m_baseMaterialCatalog.size());
        const int current = static_cast<int>(m_selectedBaseMaterialIndex);
        const int next = wrappedIndex(current + (direction < 0 ? -1 : 1), count);
        if (next == current) return newlySelected;
        m_selectedBaseMaterialIndex = static_cast<std::size_t>(next);
        return true;
    }

    const BaseMaterialCatalogEntry* TextureMap2DWorkspace::selectedBaseMaterial() const {
        if (m_selectedBaseMaterialIndex >= m_baseMaterialCatalog.size()) return nullptr;
        return &m_baseMaterialCatalog[m_selectedBaseMaterialIndex];
    }

    void TextureMap2DWorkspace::replaceBaseMaterialCatalog(
        std::vector<BaseMaterialCatalogEntry> catalog) {

        m_baseMaterialCatalog = std::move(catalog);
        if (m_selectedBaseMaterialIndex >= m_baseMaterialCatalog.size())
            m_selectedBaseMaterialIndex = 0;
    }

    void TextureMap2DWorkspace::toggleFocus() {
        m_focus = m_focus == TextureMapFocus::TextureCanvas
            ? TextureMapFocus::MeshInspection
            : TextureMapFocus::TextureCanvas;
    }

    bool TextureMap2DWorkspace::adjustPixelGridDivisions(
        int direction) {

        if (!m_target.loaded ||
            direction == 0) {

            return false;
        }

        std::size_t currentIndex = 0;

        for (std::size_t i = 0;
            i < kPixelGridDivisionPresets.size();
            i++) {

            if (kPixelGridDivisionPresets[i] ==
                m_target.pixelGridDivisions) {

                currentIndex = i;
                break;
            }
        }

        const int count =
            static_cast<int>(
                kPixelGridDivisionPresets.size()
            );

        const int step =
            direction < 0
            ? -1
            : +1;

        const std::size_t nextIndex =
            static_cast<std::size_t>(
                (
                    static_cast<int>(currentIndex) +
                    step +
                    count
                ) % count
            );

        const std::uint32_t nextDivisions =
            kPixelGridDivisionPresets[nextIndex];

        if (nextDivisions ==
            m_target.pixelGridDivisions) {

            return false;
        }

        m_target.pixelGridDivisions =
            nextDivisions;

        return true;
    }
namespace {
    template <typename Enum>
    Enum cycleWorkspaceEnum(Enum value, int count, int direction) {
        const int step = direction < 0 ? -1 : 1;
        return static_cast<Enum>(
            (static_cast<int>(value) + step + count) % count);
    }

    WorkspacePanelRow panelRow(const std::string& label,
        const std::string& value, bool selected, bool selectable = true,
        bool subordinate = false, WorkspaceStatusTone tone =
        WorkspaceStatusTone::Neutral) {
        WorkspacePanelRow row;
        row.label = label;
        row.value = value;
        row.selectable = selectable;
        row.selected = selected;
        row.subordinate = subordinate;
        row.tone = tone;
        return row;
    }

    enum TextureMapMenuCommand {
        MenuGatePrimary = 1,
        MenuTogglePanelOrView = 2,
        MenuBack = 3,
        MenuRowBase = 100
    };
}

bool TextureMap2DWorkspace::initialize(::WorkspaceServices& services) {
    m_arbiter = services.arbiter;
    m_repository = services.assetRepository;
    m_outputStaticParticlesRoot =
        services.projectRoot / "OUTPUT" / "STATIC_PARTICLES";
    m_baseMaterialsRoot =
        services.projectRoot / "INPUTS" / "TEXTURE_MAP_2D" / "BASE_MATERIALS";
    m_initialized = m_arbiter != nullptr && m_repository != nullptr &&
        services.renderer != nullptr;
    m_outputCatalog.clear();
    m_outputCatalogReady = false;
    m_baseMaterialCatalog.clear();
    m_selectedOutputIndex = 0;
    m_selectedBaseMaterialIndex = 0;
    reset();
    return m_initialized;
}

void TextureMap2DWorkspace::enter(::WorkspaceServices& services) {
    if (!m_arbiter) m_arbiter = services.arbiter;
    if (m_arbiter && m_arbiter->isDomainSelection()) {
        m_pendingHostRequest = HostRequest{};
        m_pendingHostRequest.type = HostRequestType::RefreshOutputCatalog;
    }
}

void TextureMap2DWorkspace::exit(::WorkspaceServices& services) {
    (void)services;
    endAuthoringStroke();
}

void TextureMap2DWorkspace::update(
    const ::WorkspaceFrameContext& frame,
    ::WorkspaceServices& services) {
    m_viewportWidth = frame.viewportWidth;
    m_viewportHeight = frame.viewportHeight;
    if (m_targetCameraCenterPending) {
        if (services.camera) {
            services.camera->beginTransitionToCentered2D();
			m_localCameraTransition = true;
		}
        m_targetCameraCenterPending = false;
    }
	if (m_localCameraTransition && services.camera) {
		services.camera->updatePoseTransition(frame.deltaTime);
		if (!services.camera->poseTransitionActive())
			m_localCameraTransition = false;
	}
    if (!m_planeAnimating) return;
    const float speed = 1.65f * (std::max)(0.0f, frame.deltaTime);
    if (m_planeProgress < m_planeTarget)
        m_planeProgress = (std::min)(m_planeTarget, m_planeProgress + speed);
    else
        m_planeProgress = (std::max)(m_planeTarget, m_planeProgress - speed);
    if (std::fabs(m_planeProgress - m_planeTarget) <= 0.0001f) {
        m_planeProgress = m_planeTarget;
        m_planeAnimating = false;
		updateCameraIntent(services);
    }
}

void TextureMap2DWorkspace::render(
    const ::WorkspaceFrameContext& frame,
    ::WorkspaceServices& services) {
    if (!services.renderer || !m_arbiter) return;
    EuclidRenderer& renderer = *services.renderer;

    if (m_arbiter->isDomainSelection()) {
        EuclidRenderer::UniformGrid grid;
        const float box = static_cast<float>(renderer.getSimBoxSize());
        grid.dimensions = glm::ivec3(64);
        grid.origin = glm::vec3(-box * 0.5f);
        grid.cellSize = glm::vec3(box / 64.0f);
        grid.majorEvery = 8;
        EuclidRenderer::GridDisplay display;
        display.boundary = true;
        display.majorGrid = false;
        display.minorGrid = false;
        display.axes = false;
        const float planeZ = grid.origin.z + box * m_planeProgress;
        renderer.drawUniformGridZRange(
            grid, planeZ, grid.origin.z + box, display);
        if (m_planeAnimating || m_planeProgress > 0.0f)
            renderer.drawGridPlane(
                grid, EuclidRenderer::PLANE_XY, planeZ, false);
        return;
    }

    if (!m_target.loaded ||
        m_target.assetId == INVALID_ASSET_ID ||
        m_target.readiness != TextureTargetReadiness::Ready) return;

    if (m_arbiter->isWorkspaceConfiguration()) {
        renderer.displayTextureMapStaticParticlePreview(
            frame.thetaRad, frame.phiRad, frame.objectPreviewZs,
            m_target.previewParticleRadius,
            m_layer2Item == Layer2Item::PreviewRadius,
            true, false);
        return;
    }

    if (!m_arbiter->isActiveWorkspace()) return;

    if (m_runtimeGate == RuntimeGate::Authoring &&
        m_subLayer == TextureMapSubLayer::PixelEditor &&
        m_session.workingImage.valid()) {
        std::vector<Vec2> contour;
        const float divisions =
            static_cast<float>(m_target.pixelGridDivisions);
        for (const TextureMapGridCell& cell : m_session.contourCells) {
            contour.push_back({
                (static_cast<float>(cell.x) + 0.5f) / divisions,
                (static_cast<float>(cell.y) + 0.5f) / divisions
            });
        }
        const float previewReveal = 1.0f - m_planeProgress;
        if (previewReveal > 0.001f) {
            renderer.displayTextureMapStaticParticlePreview(
                frame.thetaRad, frame.phiRad, frame.objectPreviewZs,
                m_target.previewParticleRadius,
                false, true, false, previewReveal);
        }
        if (m_planeProgress > 0.001f) {
            renderer.displayTextureMapPixelEditor(
                m_session.workingImage, m_session.selectedFace,
                m_target.pixelGridDivisions,
                m_session.cursorCell.x, m_session.cursorCell.y,
                contour, m_session.contourClosed, m_session.editorZoom,
                m_planeProgress);
        }
        return;
    }

    renderer.displayTextureMapStaticParticlePreview(
        frame.thetaRad, frame.phiRad, frame.objectPreviewZs,
        m_target.previewParticleRadius,
        false, m_runtimeGate != RuntimeGate::Authoring,
        m_runtimeGate == RuntimeGate::TargetSelected);
}

bool TextureMap2DWorkspace::handleInput(
    const ::WorkspaceInputEvent& input,
    ::WorkspaceServices& services) {
    if (!m_arbiter) m_arbiter = services.arbiter;
    if (!m_arbiter || m_planeAnimating) return true;

    if (m_arbiter->isActiveWorkspace()) {
		const bool handled = handleLayer3Input(input, services);
		if (handled) updateCameraIntent(services);
		return handled;
	}

    if (input.action == WorkspaceInputAction::Back) {
        if (m_arbiter->isWorkspaceConfiguration())
            m_arbiter->requestReturnToDomainSelection(
                TheArbiter::WorkspaceDomain::GRID_2D,
                TheArbiter::WorkspaceId::TEXTURE_MAP_2D);
        else if (m_arbiter->isDomainSelection())
            m_arbiter->requestReturnToGlobalShell(
                TheArbiter::WorkspaceDomain::GRID_2D);
        return true;
    }

    if (m_arbiter->isWorkspaceConfiguration()) {
        switch (input.action) {
        case WorkspaceInputAction::Previous:
            moveLayer2Cursor(-1); return true;
        case WorkspaceInputAction::Next:
            moveLayer2Cursor(1); return true;
        case WorkspaceInputAction::Decrease:
            if (m_layer2Item == Layer2Item::PreviewRadius)
                adjustPreviewParticleRadius(-1);
            else if (m_layer2Item == Layer2Item::PixelGrid)
                adjustPixelGridDivisions(-1);
            return true;
        case WorkspaceInputAction::Increase:
            if (m_layer2Item == Layer2Item::PreviewRadius)
                adjustPreviewParticleRadius(1);
            else if (m_layer2Item == Layer2Item::PixelGrid)
                adjustPixelGridDivisions(1);
            return true;
        case WorkspaceInputAction::Activate:
            activateLayer2(services); return true;
        default:
            return false;
        }
    }

    if (!m_arbiter->isDomainSelection()) return false;
    switch (input.action) {
    case WorkspaceInputAction::Previous:
        moveLayer1Cursor(-1); return true;
    case WorkspaceInputAction::Next:
        moveLayer1Cursor(1); return true;
    case WorkspaceInputAction::Decrease:
        adjustLayer1Value(-1, services); return true;
    case WorkspaceInputAction::Increase:
        adjustLayer1Value(1, services); return true;
    case WorkspaceInputAction::Activate:
        activateLayer1(services); return true;
    default:
        return false;
    }
}

bool TextureMap2DWorkspace::allowsInputRepeat(
    const ::WorkspaceInputEvent& input) const {
    const bool adjustment =
        input.action == WorkspaceInputAction::Decrease ||
        input.action == WorkspaceInputAction::Increase;

    return adjustment &&
        m_arbiter &&
        m_arbiter->isActiveWorkspace() &&
        m_runtimeGate == RuntimeGate::Authoring &&
        m_subLayer == TextureMapSubLayer::PixelEditor &&
        m_session.viewMode == TextureMapViewMode::Edit &&
        m_authoringMode == TextureMapAuthoringMode::Coloring &&
        m_runtimeRow >= 1 &&
        m_runtimeRow <= 4;
}

void TextureMap2DWorkspace::moveLayer1Cursor(int direction) {
    m_layer1Item = cycleWorkspaceEnum(
        m_layer1Item, static_cast<int>(Layer1Item::Count), direction);
}

void TextureMap2DWorkspace::adjustLayer1Value(
    int direction, ::WorkspaceServices& services) {
    if (m_layer1Item == Layer1Item::Workspace) {
        if (services.arbiter)
            services.arbiter->setActiveWorkspace(
                TheArbiter::WorkspaceId::GRAPH_2D);
        return;
    }
    if (m_layer1Item == Layer1Item::Target &&
        selectOutputAsset(direction)) {
        m_loadedTargetDisplayName.clear();
        m_layerStatusMessage.clear();
        if (services.camera) {
            services.camera->beginTransitionToStandard2D();
            m_localCameraTransition = true;
        }
    }
}

void TextureMap2DWorkspace::activateLayer1(
    ::WorkspaceServices& services) {
    if (m_layer1Item == Layer1Item::Target) return;
    if (m_layer1Item == Layer1Item::LoadTarget) {
        const StaticAssetCatalogEntry* selected = selectedOutputAsset();
        if (!selected || !selected->valid) {
            m_layerStatusMessage = selected
                ? "SELECTED STATIC PARTICLE MANIFEST IS INVALID."
                : "NO OUTPUT STATIC PARTICLE ASSETS FOUND.";
            return;
        }
        m_pendingHostRequest = HostRequest{};
        m_pendingHostRequest.type = HostRequestType::LoadTarget;
        m_pendingHostRequest.target = *selected;
        m_pendingHostRequest.replaceAssetId = m_target.assetId;
        m_layerStatusMessage =
            "LOADING TARGET: " + selected->displayName + "...";
        return;
    }
    if (m_layer1Item == Layer1Item::Configure) {
        if (!m_target.loaded ||
            m_target.readiness != TextureTargetReadiness::Ready) {
            m_layerStatusMessage =
                "TARGET CONFIGURATION LOCKED:\nLOAD A READY TARGET FIRST.";
            return;
        }
        if (services.arbiter)
            services.arbiter->requestEnterWorkspaceConfiguration(
                TheArbiter::WorkspaceDomain::GRID_2D,
                TheArbiter::WorkspaceId::TEXTURE_MAP_2D);
    }
}

void TextureMap2DWorkspace::moveLayer2Cursor(int direction) {
    m_layer2Item = cycleWorkspaceEnum(
        m_layer2Item, static_cast<int>(Layer2Item::Count), direction);
}

void TextureMap2DWorkspace::activateLayer2(
    ::WorkspaceServices& services) {
    if (m_layer2Item != Layer2Item::RunWorkspaceEdit) return;
    if (!m_target.loaded ||
        m_target.readiness != TextureTargetReadiness::Ready) {
        m_layerStatusMessage = "TEXTURE MAP TARGET IS NOT READY.";
        return;
    }
    resetLayer3Runtime();
    if (services.arbiter)
        services.arbiter->requestEnterActiveWorkspace(
            TheArbiter::WorkspaceDomain::GRID_2D,
            TheArbiter::WorkspaceId::TEXTURE_MAP_2D);
}

void TextureMap2DWorkspace::resetLayer3Runtime() {
    m_runtimeGate = RuntimeGate::ReferencePreview;
    m_runtimeRow = 0;
    m_runtimePanelVisible = false;
    m_runtimeStatusMessage.clear();
    m_session.clear();
    m_subLayer = TextureMapSubLayer::CycleSetup;
    m_planeProgress = 0.0f;
    m_planeTarget = 0.0f;
    m_planeAnimating = false;
	m_localCameraTransition = false;
	m_selectionPointerValid = false;
	m_selectionPointerX = 0;
	m_selectionPointerY = 0;
}

void TextureMap2DWorkspace::moveRuntimeCursor(int direction) {
    if (m_nestedFocus) return;
    const int count = (std::max)(1, runtimeRowCount());
    m_runtimeRow =
        (m_runtimeRow + (direction < 0 ? -1 : 1) + count) % count;
}

bool TextureMap2DWorkspace::pointerToEditorCell(
    int x, int y, int& cellX, int& cellY) const {
    const int canvas = static_cast<int>((std::min)(
        static_cast<float>(m_viewportHeight) * 0.72f,
        static_cast<float>(m_viewportWidth) * 0.55f) *
        m_session.editorZoom);
    const int left =
        static_cast<int>(static_cast<float>(m_viewportWidth) * 0.62f) -
        canvas / 2;
    const int top = (m_viewportHeight - canvas) / 2;
    const int divisions = static_cast<int>(m_target.pixelGridDivisions);
    cellX = canvas > 0 ? (x - left) * divisions / canvas : -1;
    cellY = canvas > 0
        ? (top + canvas - 1 - y) * divisions / canvas : -1;
    return x >= left && x < left + canvas &&
        y >= top && y < top + canvas;
}

void TextureMap2DWorkspace::reloadPreviewMesh(
    ::WorkspaceServices& services) const {
    if (!services.renderer) return;
    StaticParticleAsset preview = buildPreviewAsset();
    if (!preview.mesh.empty())
        services.renderer->loadParticleStaticAsset(preview);
}

void TextureMap2DWorkspace::requestReturnToLayer2(
    ::WorkspaceServices& services) {
    if (services.arbiter)
        services.arbiter->requestReturnToWorkspaceConfiguration(
            TheArbiter::WorkspaceDomain::GRID_2D,
            TheArbiter::WorkspaceId::TEXTURE_MAP_2D);
}

void TextureMap2DWorkspace::updateCameraIntent(
	::WorkspaceServices& services) const {
	if (!services.camera || !m_arbiter) return;

	CameraProcessor::CameraBehaviorMode mode =
		CameraProcessor::CAM_STANDARD_2D_LOCKED;
	if (m_arbiter->isWorkspaceConfiguration()) {
		mode = CameraProcessor::CAM_STANDARD_OBJECT_ORBIT;
	}
	else if (m_arbiter->isActiveWorkspace()) {
		const bool planarEdit =
			m_runtimeGate == RuntimeGate::Authoring &&
			m_subLayer == TextureMapSubLayer::PixelEditor &&
			m_session.viewMode == TextureMapViewMode::Edit;
		if (planarEdit)
			mode = CameraProcessor::CAM_STANDARD_2D_LOCKED;
		else if (m_runtimeGate == RuntimeGate::SelectionArmed)
			mode = CameraProcessor::CAM_STANDARD_OBJECT_SELECTION_LOCKED;
		else
			mode = CameraProcessor::CAM_STANDARD_OBJECT_ORBIT;
	}
	services.camera->setBehaviorMode(mode);
}

bool TextureMap2DWorkspace::handleLayer3Input(
    const ::WorkspaceInputEvent& input,
    ::WorkspaceServices& services) {
    if (input.action == WorkspaceInputAction::Back) {
        if (m_runtimeGate != RuntimeGate::Authoring) {
            requestReturnToLayer2(services);
            return true;
        }
        std::string diagnostic;
        if (canExitLayer3(&diagnostic)) requestReturnToLayer2(services);
        else m_runtimeStatusMessage = diagnostic;
        return true;
    }

    if (m_runtimeGate != RuntimeGate::Authoring) {
        if (input.action == WorkspaceInputAction::Activate) {
            if (m_runtimeGate == RuntimeGate::ReferencePreview)
                m_runtimeGate = RuntimeGate::SelectionArmed;
            else if (m_runtimeGate == RuntimeGate::SelectionArmed)
                m_runtimeGate = RuntimeGate::ReferencePreview;
            else
                m_runtimeGate = RuntimeGate::ReferencePreview;
			m_selectionPointerValid = false;
            if (services.renderer)
                services.renderer->setParticleHighlighted(false);
            return true;
        }
        if (input.action == WorkspaceInputAction::Toggle) {
            if (m_runtimeGate != RuntimeGate::TargetSelected) {
                m_runtimeStatusMessage = "SELECT A TARGET FIRST";
                return true;
            }
            if (beginAuthoringRuntime()) {
                m_runtimeGate = RuntimeGate::Authoring;
                m_runtimePanelVisible = true;
                m_runtimeRow = 0;
                reloadPreviewMesh(services);
            }
            return true;
        }
        if (input.action == WorkspaceInputAction::PointerDown &&
			input.button == GLUT_LEFT_BUTTON &&
			m_runtimeGate == RuntimeGate::SelectionArmed) {
			m_selectionPointerX = input.x;
			m_selectionPointerY = input.y;
			m_selectionPointerValid = true;
            if (services.renderer &&
                services.renderer->hitTestTextureMapPreview(input.x, input.y)) {
                m_runtimeGate = RuntimeGate::TargetSelected;
				m_selectionPointerValid = false;
                services.renderer->setParticleHighlighted(true);
                m_runtimeStatusMessage.clear();
            }
            return true;
        }
        if (input.action == WorkspaceInputAction::PointerMove &&
			m_runtimeGate == RuntimeGate::SelectionArmed) {
			m_selectionPointerX = input.x;
			m_selectionPointerY = input.y;
			m_selectionPointerValid = true;
            return true;
		}
        return input.action == WorkspaceInputAction::PointerUp;
    }

    if (input.action == WorkspaceInputAction::Toggle) {
        if (m_subLayer == TextureMapSubLayer::PixelEditor) {
            if (toggleRuntimeView()) {
				m_planeTarget =
                    m_session.viewMode == TextureMapViewMode::Edit
                    ? 1.0f : 0.0f;
                m_planeAnimating = true;
                m_runtimePanelVisible =
                    m_session.viewMode == TextureMapViewMode::Edit;
                if (m_session.viewMode == TextureMapViewMode::Preview) {
                    reloadPreviewMesh(services);
					if (services.camera) {
						services.camera->beginTransitionToStandardObject();
						m_localCameraTransition = true;
					}
				}
				else if (services.camera) {
					services.camera->beginTransitionToStandard2D();
					m_localCameraTransition = true;
				}
            }
        }
        else if (m_subLayer == TextureMapSubLayer::CommitSave) {
            m_runtimePanelVisible = !m_runtimePanelVisible;
        }
        // Intentional improvement: Sub-Layers 0 and 1 keep their panel.
        return true;
    }

    if (input.action == WorkspaceInputAction::Previous) {
        moveRuntimeCursor(-1); return true;
    }
    if (input.action == WorkspaceInputAction::Next) {
        moveRuntimeCursor(1); return true;
    }
    if (input.action == WorkspaceInputAction::Decrease ||
        input.action == WorkspaceInputAction::Increase) {
        const bool changed = adjustRuntimeValue(
            m_runtimeRow,
            input.action == WorkspaceInputAction::Decrease ? -1 : 1);
        if (changed) reloadPreviewMesh(services);
        return true;
    }
    if (input.action == WorkspaceInputAction::ZoomIn ||
        input.action == WorkspaceInputAction::ZoomOut) {
        if (m_subLayer == TextureMapSubLayer::PixelEditor &&
            m_session.viewMode == TextureMapViewMode::Edit) {
            adjustEditorZoom(
                input.action == WorkspaceInputAction::ZoomIn ? 1 : -1);
            return true;
        }
        return false;
    }
    if (input.action == WorkspaceInputAction::Activate) {
        std::string diagnostic;
        const TextureMapSubLayer previousSubLayer = m_subLayer;
        const TextureMap2DWorkspaceAction action =
            activateRuntimeRow(m_runtimeRow, &diagnostic);
        if (action == TextureMap2DWorkspaceAction::RequestSurfaceTargetName) {
            m_pendingHostRequest = HostRequest{};
            m_pendingHostRequest.type =
                HostRequestType::RequestSurfaceTargetName;
        }
        else if (action == TextureMap2DWorkspaceAction::RequestSaveCurrent) {
            m_pendingHostRequest = HostRequest{};
            m_pendingHostRequest.type = HostRequestType::SaveCurrent;
        }
        else if (action == TextureMap2DWorkspaceAction::RequestSaveAs) {
            m_pendingHostRequest = HostRequest{};
            const bool unnamedContour = m_session.dirty &&
                m_authoringMode == TextureMapAuthoringMode::Contour &&
                m_contourAction == TextureMapContourAction::New;
            m_pendingHostRequest.type = unnamedContour
                ? HostRequestType::RequestSurfaceTargetNameThenSaveAs
                : HostRequestType::SaveAs;
            m_pendingHostRequest.suggestedName = loadedTargetName();
        }
        if (!diagnostic.empty()) m_runtimeStatusMessage = diagnostic;
		if (m_subLayer == TextureMapSubLayer::PixelEditor &&
			previousSubLayer != TextureMapSubLayer::PixelEditor) {
			m_session.viewMode = TextureMapViewMode::Edit;
			m_planeProgress = 1.0f;
			m_planeTarget = 1.0f;
			m_planeAnimating = false;
			if (services.camera)
			{
				services.camera->beginTransitionToStandard2D();
				m_localCameraTransition = true;
			}
		}
		else if (previousSubLayer == TextureMapSubLayer::PixelEditor &&
			m_subLayer != TextureMapSubLayer::PixelEditor) {
			m_planeProgress = 0.0f;
			m_planeTarget = 0.0f;
			m_planeAnimating = false;
			if (services.camera) {
				services.camera->beginTransitionToStandardObject();
				m_localCameraTransition = true;
			}
		}
        m_runtimeRow = (std::min)(
            m_runtimeRow, (std::max)(0, runtimeRowCount() - 1));
        m_runtimePanelVisible = true;
        reloadPreviewMesh(services);
        return true;
    }

    if (m_subLayer == TextureMapSubLayer::PixelEditor &&
        m_session.viewMode == TextureMapViewMode::Edit) {
        int cellX = -1;
        int cellY = -1;
        const bool inside =
            pointerToEditorCell(input.x, input.y, cellX, cellY);
        if (input.action == WorkspaceInputAction::PointerMove) {
            setCursorCell(cellX, cellY);
            continueAuthoringStroke(cellX, cellY);
            reloadPreviewMesh(services);
            return true;
        }
        if (input.action == WorkspaceInputAction::PointerDown) {
            if (inside) beginAuthoringStroke(cellX, cellY);
            reloadPreviewMesh(services);
            return true;
        }
        if (input.action == WorkspaceInputAction::PointerUp) {
            endAuthoringStroke();
            return true;
        }
    }
    return false;
}

bool TextureMap2DWorkspace::automaticTransitionActive() const {
    return m_planeAnimating || m_localCameraTransition;
}

const char* TextureMap2DWorkspace::authoringModeName() const {
    switch (m_authoringMode) {
    case TextureMapAuthoringMode::Contour: return "CONTOUR";
    case TextureMapAuthoringMode::PanelLines: return "PANEL_LINES";
    default: return "COLORING";
    }
}

const char* TextureMap2DWorkspace::channelName() const {
    switch (m_selectedChannel) {
    case TextureMapChannel::EmissiveColor: return "EMISSIVE_COLOR";
    case TextureMapChannel::AlphaMask: return "ALPHA_MASK";
    case TextureMapChannel::Height: return "HEIGHT";
    case TextureMapChannel::Normal: return "NORMAL";
    case TextureMapChannel::MetallicRoughness:
        return "METALLIC_ROUGHNESS";
    case TextureMapChannel::Occlusion: return "OCCLUSION";
    default: return "BASE_COLOR";
    }
}

std::string TextureMap2DWorkspace::loadedTargetName() const {
    const StaticParticleAsset* asset = m_repository
        ? m_repository->findStaticParticle(m_target.assetId) : nullptr;
    if (asset && !asset->name.empty()) return asset->name;
    if (!m_loadedTargetDisplayName.empty())
        return m_loadedTargetDisplayName;
    return "UNAVAILABLE";
}

std::string TextureMap2DWorkspace::selectedSurfaceName() const {
    const StaticParticleAsset* asset = m_repository
        ? m_repository->findStaticParticle(m_target.assetId) : nullptr;
    if (!asset || m_selectedSurfaceTarget < 0) return "default";
    const std::size_t index =
        static_cast<std::size_t>(m_selectedSurfaceTarget);
    return index < asset->surfaceTargets.size()
        ? asset->surfaceTargets[index].name : "default";
}

::WorkspacePresentation TextureMap2DWorkspace::buildPresentation() const {
    if (m_arbiter && m_arbiter->isActiveWorkspace())
        return buildLayer3Presentation();
    if (m_arbiter && m_arbiter->isWorkspaceConfiguration())
        return buildLayer2Presentation();
    return buildLayer1Presentation();
}

::WorkspacePresentation
TextureMap2DWorkspace::buildLayer1TransitionPresentation() const {
    return buildLayer1Presentation();
}

::WorkspacePresentation TextureMap2DWorkspace::buildLayer1Presentation() const {
    WorkspacePresentation p;
    p.panelVisible = true;
    p.workspaceName = "LAYER 1 -> GRID_2D WORKSPACE CONFIGURATION";
    p.layerLabel = "MODE: TEXTURE_MAP_2D";
    WorkspacePanelSection section;
    section.rows.push_back(panelRow(
        "[1]: GRID_2D SELECTION", "TEXTURE_MAP_2D",
        m_layer1Item == Layer1Item::Workspace));

    const StaticAssetCatalogEntry* selected = selectedOutputAsset();
    std::string targetName = "NO OUTPUT ASSETS";
    std::string badge = m_outputCatalogReady
        ? "NO OUTPUT ASSETS" : "CATALOG PENDING";
    if (selected) {
        targetName = selected->displayName;
        if (m_target.loaded &&
            selected->displayName == m_loadedTargetDisplayName) {
            badge = m_target.readiness == TextureTargetReadiness::Ready
                ? "LOADED | READY" : "LOADED | NOT READY";
        }
        else badge = selected->valid
            ? "SELECTED | READY" : "SELECTED | INVALID";
    }
    section.rows.push_back(panelRow(
        "[2]: TARGET SP", targetName + " [ " + badge + " ]",
        m_layer1Item == Layer1Item::Target));
    section.rows.push_back(panelRow(
        "[3]: LOAD TARGET ASSET", "",
        m_layer1Item == Layer1Item::LoadTarget));
    section.rows.push_back(panelRow(
        "[4]: PRESS E TO CONFIGURE SIM", "",
        m_layer1Item == Layer1Item::Configure));
    p.sections.push_back(section);

    if (m_target.loaded &&
        m_target.readiness == TextureTargetReadiness::Ready) {
        p.statusLine = "TARGET LOADED: " + loadedTargetName() +
            "\nTEXTURE_MAP_2D CONFIGURATION READY.";
        p.statusTone = WorkspaceStatusTone::Ready;
    }
    else if (!m_layerStatusMessage.empty()) {
        p.statusLine = m_layerStatusMessage;
        p.statusTone = WorkspaceStatusTone::Warning;
    }
    else if (!m_outputCatalogReady) {
        p.statusLine = "OUTPUT CATALOG IS NOT INITIALIZED.";
        p.statusTone = WorkspaceStatusTone::Warning;
    }
    else if (m_outputCatalog.empty()) {
        p.statusLine = "NO OUTPUT STATIC PARTICLE ASSETS FOUND.";
        p.statusTone = WorkspaceStatusTone::Warning;
    }
    else if (selected) {
        p.statusLine = selected->valid
            ? "SELECTED TARGET: " + selected->displayName +
                "\nLOAD TARGET ASSET WITH ROW [3]."
            : "SELECTED TARGET INVALID: " + selected->displayName +
                "\nCONFIGURATION LOCKED.";
        p.statusTone = selected->valid
            ? WorkspaceStatusTone::Caution
            : WorkspaceStatusTone::Warning;
    }
    p.footerLine1 =
        "W / S: Select list     A / D: Change value";
    p.footerLine2 =
        "E: Activate selected row     Q: Back one layer";
    return p;
}

::WorkspacePresentation TextureMap2DWorkspace::buildLayer2Presentation() const {
    WorkspacePresentation p;
    p.panelVisible = true;
    p.workspaceName =
        "LAYER 2 -> TEXTURE_MAP_2D TARGET CONFIGURATION";
    p.layerLabel = "MODE: TEXTURE_MAP_2D";
    WorkspacePanelSection targetSection;
    targetSection.heading = "TARGET SP:";
    targetSection.rows.push_back(panelRow(
        "{ " + loadedTargetName() + " }",
        m_target.readiness == TextureTargetReadiness::Ready
            ? "[ LOADED | READY ]" : "[ UNAVAILABLE ]",
        false, false));
    targetSection.notes.push_back("3D PREVIEW:");
    targetSection.notes.push_back("{ TEXTURED STATIC PARTICLE }");
    p.sections.push_back(targetSection);

    WorkspacePanelSection configuration;
    char radius[32]{};
    std::snprintf(radius, sizeof(radius), "%.4f",
        m_target.previewParticleRadius);
    configuration.rows.push_back(panelRow(
        "[1]: PREVIEW PARTICLE RADIUS", radius,
        m_layer2Item == Layer2Item::PreviewRadius));
    const std::string grid =
        std::to_string(m_target.pixelGridDivisions) + " x " +
        std::to_string(m_target.pixelGridDivisions);
    configuration.rows.push_back(panelRow(
        "[2]: PIXEL GRID", grid,
        m_layer2Item == Layer2Item::PixelGrid));
    configuration.rows.push_back(panelRow(
        "[3]: RUN WORKSPACE EDIT", "",
        m_layer2Item == Layer2Item::RunWorkspaceEdit));
    p.sections.push_back(configuration);
    if (!m_layerStatusMessage.empty()) {
        p.statusLine = m_layerStatusMessage;
        p.statusTone = m_target.loaded &&
			m_target.readiness == TextureTargetReadiness::Ready
			? WorkspaceStatusTone::Ready
			: WorkspaceStatusTone::Warning;
    }
    p.footerLine1 =
        "Q: TARGET SELECTION     W/S: SELECT     A/D: CHANGE VALUE";
    p.footerLine2 =
        "E: ACTIVATE     MOUSE: ORBIT / ZOOM PREVIEW";
    return p;
}

::WorkspacePresentation TextureMap2DWorkspace::buildLayer3Presentation() const {
    WorkspacePresentation p;
    p.panelLayout = WorkspacePanelLayout::SubLayer;
    p.subLayerPanelLayout =
        WorkspaceSubLayerPanelLayout::TextureMapCompact;
    p.panelVisible =
        m_runtimeGate == RuntimeGate::Authoring &&
        m_runtimePanelVisible;
    p.workspaceName = "TEXTURE_MAP_2D MODE";
    p.runtimeStatus.visible = true;
    p.runtimeStatus.titleLine =
        "LAYER 3 -> TEXTURE_MAP_2D WORKSPACE RUNTIME";
    const std::string targetName = loadedTargetName();

    if (m_runtimeGate != RuntimeGate::Authoring) {
		p.selectionCursor.visible =
			m_runtimeGate == RuntimeGate::SelectionArmed &&
			m_selectionPointerValid;
		p.selectionCursor.x = m_selectionPointerX;
		p.selectionCursor.y = m_selectionPointerY;
        const char* gate = m_runtimeGate == RuntimeGate::TargetSelected
            ? "TARGET SELECTED"
            : m_runtimeGate == RuntimeGate::SelectionArmed
            ? "SELECTION ARMED" : "REFERENCE PREVIEW";
        p.runtimeStatus.contextLine =
            std::string("TEXTURE_MAP_2D: ") + gate;
        p.runtimeStatus.objectLine =
            "TARGET: SP { " + targetName +
            " } | RUNTIME GATE { " + gate + " }";
        if (!m_runtimeStatusMessage.empty())
            p.runtimeStatus.contextLine =
                "TEXTURE_MAP_2D: " + m_runtimeStatusMessage;
        if (m_runtimeGate == RuntimeGate::TargetSelected)
            p.runtimeStatus.helpLine =
                "E: Deselect target    TAB: Authoring cycle panel    Q: Back";
        else if (m_runtimeGate == RuntimeGate::SelectionArmed)
            p.runtimeStatus.helpLine =
                "LMB: Select target mesh    E: Cancel selection mode    Q: Back";
        else
            p.runtimeStatus.helpLine =
                "E: Arm target selection    MOUSE: Orbit / Zoom    Q: Back";
        return p;
    }

    const std::string mode = authoringModeName();
    const std::string status = m_session.dirty ? "DIRTY" : "CLEAN";
    const std::string grid =
        std::to_string(m_target.pixelGridDivisions);
    if (m_subLayer == TextureMapSubLayer::CycleSetup)
        p.subLayerLabel =
            "SUB-LAYER_0 -> AUTHORING CYCLE SETUP";
    else if (m_subLayer == TextureMapSubLayer::BranchSetup)
        p.subLayerLabel =
            "SUB-LAYER_1 -> " + mode + " SETUP";
    else if (m_subLayer == TextureMapSubLayer::PixelEditor)
        p.subLayerLabel =
            "SUB-LAYER_2 -> PIXEL GRID " + mode;
    else
        p.subLayerLabel =
            "SUB-LAYER_3 -> COMMIT AUTHORING PASS";

    p.runtimeStatus.contextLine =
        "TEXTURE_MAP_2D: " + p.subLayerLabel;
    p.runtimeStatus.objectLine =
        "TARGET: SP { " + targetName + " } | AUTHORING { " +
        mode + " } | STATUS { " + status + " }";
    p.runtimeStatus.helpLine = !p.panelVisible
        ? "TAB: Restore authoring panel    MOUSE: Orbit / Zoom    Q: Back"
        : m_subLayer == TextureMapSubLayer::PixelEditor
        ? "TAB: Edit / Preview    W/S: Select item    A/D: Change value    E: Activate    Q: Back"
        : "W/S: Select item    A/D: Change value    E: Activate    Q: Back";

    const StaticParticleAsset* asset = m_repository
        ? m_repository->findStaticParticle(m_target.assetId) : nullptr;
    const MaterialSlot* material = asset &&
        m_target.materialIndex < asset->materials.size()
        ? &asset->materials[m_target.materialIndex] : nullptr;
    const std::string materialName = material
        ? material->name : "Default Material";
    const TextureMapChannel presentationChannel =
        m_authoringMode == TextureMapAuthoringMode::PanelLines
        ? TextureMapChannel::BaseColor : m_selectedChannel;
    const TextureResource* texture = nullptr;
    if (asset && material) {
        const std::string& id =
            presentationChannel == TextureMapChannel::EmissiveColor
            ? material->emissiveTextureId
            : material->baseColorTextureId;
        texture = asset->findTexture(id);
    }
    std::string textureName = texture
        ? std::filesystem::path(texture->relativePath).filename().string()
        : presentationChannel == TextureMapChannel::EmissiveColor
        ? "blank_working_emissive" : "UNAVAILABLE";
    std::uint32_t textureWidth = texture ? texture->width : 0u;
    std::uint32_t textureHeight = texture ? texture->height : 0u;
    if (presentationChannel == TextureMapChannel::BaseColor &&
        hasSelectedBaseMaterialSource()) {
        const BaseMaterialCatalogEntry* source = selectedBaseMaterial();
        if (source) {
            textureName = source->baseColorPath.filename().string();
            textureWidth = source->width;
            textureHeight = source->height;
        }
    }
    const std::string resolution =
        textureWidth > 0u && textureHeight > 0u
        ? std::to_string(textureWidth) + " x " +
            std::to_string(textureHeight)
        : "PENDING";
    const auto info = [](const std::string& label,
        bool subordinate = true, bool emphasized = false) {
        WorkspacePanelRow row = panelRow(
            label, "", false, false, subordinate,
            emphasized ? WorkspaceStatusTone::Ready
                : WorkspaceStatusTone::Neutral);
        row.emphasized = emphasized;
        return row;
    };
    const auto selectable = [&](const std::string& label, int row,
        bool subordinate = false, bool nested = false) {
        return panelRow(label, "",
            m_runtimeRow == row && m_nestedFocus == nested,
            true, subordinate);
    };
    const auto addSection = [&](const std::string& heading,
        std::vector<WorkspacePanelRow> rows,
        WorkspacePresentation& target) {
        WorkspacePanelSection section;
        section.heading = heading;
        section.rows = std::move(rows);
        target.sections.push_back(std::move(section));
    };

    if (m_subLayer == TextureMapSubLayer::CycleSetup) {
        addSection("Target Object:", {
            info("SP { " + targetName + " }")
        }, p);
        addSection("Authoring Pass:", {
            selectable("[1] AUTHORING MODE { " + mode + " }", 0),
            selectable("[2] PREVIEW SOURCE { " +
                std::string(m_previewSource ==
                    TextureMapPreviewSource::Working
                    ? "WORKING" : "COMMITTED") + " }", 1)
        }, p);
        addSection("Working Session:", {
            info("STATUS { " + status + " }"),
            info("PIXEL GRID { " + grid + " x " + grid + " }")
        }, p);
        addSection("Next Sub-Layer:", {
            selectable("[3] CONFIGURE AUTHORING PASS", 2),
            info("-> SUB-LAYER 1")
        }, p);
    }
    else if (m_subLayer == TextureMapSubLayer::BranchSetup) {
        addSection("Target Object:", {
            info("SP { " + targetName + " }")
        }, p);
        if (m_authoringMode == TextureMapAuthoringMode::Contour) {
            std::string contourTarget = "NEW";
            if (m_contourAction ==
                TextureMapContourAction::EditExisting) {
                contourTarget = "NONE AVAILABLE";
                if (asset && !asset->surfaceTargets.empty()) {
                    const std::size_t index =
                        static_cast<std::size_t>((std::max)(
                            0, m_selectedContourTarget)) %
                        asset->surfaceTargets.size();
                    contourTarget =
                        asset->surfaceTargets[index].name;
                }
            }
            addSection("Surface Target Authoring:", {
                selectable("[1] CONTOUR ACTION { " +
                    std::string(m_contourAction ==
                        TextureMapContourAction::New
                        ? "NEW" : "EDIT EXISTING") + " }", 0),
                selectable("[2] CONTOUR TARGET { " +
                    contourTarget + " }", 1)
            }, p);
        }
        else {
            std::vector<WorkspacePanelRow> editRows;
            editRows.push_back(selectable(
                "[1] SURFACE TARGET { " +
                selectedSurfaceName() + " }", 0));
            editRows.push_back(info(
                "MATERIAL SLOT { " + materialName + " }"));
            const std::string active =
                channelActive(m_selectedChannel)
                ? "ACTIVE" : "INACTIVE";
            editRows.push_back(selectable(
                "[2] TEXTURE CHANNEL { " +
                std::string(m_authoringMode ==
                    TextureMapAuthoringMode::PanelLines
                    ? "BASE_COLOR" : channelName()) +
                " } [ " + active + " ]", 1));
            if (m_authoringMode == TextureMapAuthoringMode::Coloring &&
                channelActive(m_selectedChannel)) {
                if (m_selectedChannel ==
                    TextureMapChannel::EmissiveColor) {
                    char intensity[32]{};
                    std::snprintf(intensity, sizeof(intensity),
                        "%.2f", emissiveIntensity());
                    editRows.push_back(selectable(
                        std::string("INTENSITY { ") +
                        intensity + " }", 1, true, true));
                }
                else editRows.push_back(selectable(
                    "TEXTURE MAP { " + textureName + " }",
                    1, true, true));
            }
            else if (m_authoringMode ==
                TextureMapAuthoringMode::Coloring)
                editRows.push_back(info(
                    "SELECTED CHANNEL IS RESERVED / INACTIVE"));
            else editRows.push_back(info(
                "TEXTURE MAP { " + textureName + " }"));
            editRows.push_back(info(
                "RESOLUTION { " + resolution + " } [ READY ]",
                true, true));
            editRows.push_back(info("UV MAP { TEXCOORD_0 }"));
            editRows.push_back(info(
                "SOURCE { BOX_ATLAS_6_DIRECTION } [ READY ]",
                true, true));
            addSection(m_authoringMode ==
                TextureMapAuthoringMode::PanelLines
                ? "Panel Line Surface:" : "Edit Surface Texture:",
                std::move(editRows), p);
        }
        const std::string action =
            m_authoringMode == TextureMapAuthoringMode::Contour
            ? "DRAW CONTOUR"
            : m_authoringMode == TextureMapAuthoringMode::PanelLines
            ? "DRAW PANEL LINES" : "DRAW PIXEL GRID";
        addSection("Next / Prev Sub-Layers:", {
            selectable("[3] " + action, 2),
            info("-> SUB-LAYER 2"),
            selectable("[4] AUTHORING CYCLE SETUP", 3),
            info("-> SUB-LAYER 0")
        }, p);
    }
    else if (m_subLayer == TextureMapSubLayer::PixelEditor) {
        addSection("Target Object:", {
            info("SP { " + targetName + " }")
        }, p);
        addSection("Editing Face:", {
            selectable("[1] SELECT FACE { FACE_" +
                std::to_string(m_session.selectedFace) + " | " +
                boxAtlasFaceAxisName(m_session.selectedFace) + " }", 0)
        }, p);
        int reviewRow = 3;
        if (m_authoringMode == TextureMapAuthoringMode::Coloring) {
            addSection("Edit Pixel Color:", {
                selectable("[2] RED { " +
                    std::to_string(m_session.paintColor[0]) + " }", 1),
                selectable("[3] GREEN { " +
                    std::to_string(m_session.paintColor[1]) + " }", 2),
                selectable("[4] BLUE { " +
                    std::to_string(m_session.paintColor[2]) + " }", 3),
                selectable("[5] ALPHA { " +
                    std::to_string(m_session.paintColor[3]) + " }", 4)
            }, p);
            reviewRow = 5;
        }
        else if (m_authoringMode ==
            TextureMapAuthoringMode::Contour) {
            addSection("Contour Edit:", {
                selectable("[2] CLOSE CONTOUR", 1),
                selectable("[3] UNDO LAST POINT", 2)
            }, p);
            addSection("Working Contour:", {
                info("STATUS { " + std::string(
                    m_session.contourClosed
                    ? "CLOSED | READY" : "OPEN") + " }"),
                info("POINTS { " +
                    std::to_string(m_session.contourCells.size()) + " }")
            }, p);
        }
        else {
            const TextureMapLineColorPreset& preset =
                lineColorPresets()[m_session.lineColorPreset];
            addSection("Panel Line Tool:", {
                selectable("[2] LINE THICKNESS { " +
                    std::to_string(m_session.lineThickness) + " }", 1),
                selectable("[3] LINE COLOR { " +
                    std::string(preset.name) + " }", 2)
            }, p);
            addSection("Working Panel Lines:", {
                info("STATUS { " + status + " }")
            }, p);
        }
        addSection("Next / Prev Sub-Layers:", {
            selectable("[" + std::to_string(reviewRow + 1) +
                "] REVIEW / COMMIT", reviewRow),
            info("-> SUB-LAYER 3"),
            selectable("[" + std::to_string(reviewRow + 2) +
                "] " + mode + " SETUP", reviewRow + 1),
            info("-> SUB-LAYER 1")
        }, p);
    }
    else {
        addSection("Working Output:", {
            info("TARGET { " + targetName + " }"),
            info("AUTHORING { " + mode + " }"),
            info("STATUS { " + status + " }")
        }, p);
        addSection("Apply Working Edit:", {
            selectable("[1] COMMIT WORKING EDIT TO TARGET", 0)
        }, p);
        addSection("Asset Output:", {
            selectable("[2] SAVE CURRENT SP_ASSET", 1),
            selectable("[3] SAVE STATIC PARTICLE AS", 2)
        }, p);
        addSection("Previous Sub-Layer:", {
            selectable("[4] RETURN TO PIXEL GRID", 3)
        }, p);
        addSection("Next Cycle:", {
            selectable(
                "[5] RETURN TO AUTHORING CYCLE SETUP", 4)
        }, p);
    }

    if (m_subLayer == TextureMapSubLayer::CommitSave) {
        p.footerLine1 =
            "W/S: Select    E: Activate    Q: Structural layer navigation";
    }
    else if (m_subLayer == TextureMapSubLayer::PixelEditor) {
        p.footerLine1 =
            "W/S: Select    A/D: Change value    E: Activate    TAB: Edit / Preview    Q: Back";
    }
    else {
        p.footerLine1 =
            "W/S: Select    A/D: Change value    E: Activate    Q: Back";
    }
    p.footerLine2 = m_runtimeStatusMessage.empty()
        ? "Q: STRUCTURAL LAYER NAVIGATION | DIRTY EXIT IS BLOCKED"
        : m_runtimeStatusMessage;
    return p;
}

::WorkspaceMenuPresentation TextureMap2DWorkspace::buildMenu() const {
    WorkspaceMenuPresentation menu;
    const auto add = [&](const std::string& label, int command,
        bool enabled = true) {
        WorkspaceMenuItem item;
        item.label = label;
        item.command = command;
        item.enabled = enabled;
        menu.items.push_back(std::move(item));
    };
    if (!m_arbiter || !m_arbiter->isActiveWorkspace()) return menu;
    if (m_runtimeGate != RuntimeGate::Authoring) {
        add(m_runtimeGate == RuntimeGate::TargetSelected
            ? "* Deselect Target Mesh"
            : m_runtimeGate == RuntimeGate::SelectionArmed
            ? "* Cancel Target Selection"
            : "* Arm Target Selection", MenuGatePrimary);
        if (m_runtimeGate == RuntimeGate::TargetSelected)
            add("* Show Authoring Panel", MenuTogglePanelOrView);
        add("* Back to Layer 2", MenuBack);
        return menu;
    }
    if (!m_runtimePanelVisible) {
        add(m_subLayer == TextureMapSubLayer::PixelEditor
            ? "* Return to Pixel Editor"
            : "* Show Authoring Panel",
            MenuTogglePanelOrView);
        add("* Back to Layer 2", MenuBack);
        return menu;
    }
    const int count = runtimeRowCount();
    for (int row = 0; row < count; row++) {
        std::string label =
            row == m_runtimeRow ? "* " : "  ";
        label += "Activate Row [" + std::to_string(row + 1) + "]";
        add(label, MenuRowBase + row);
    }
    if (m_subLayer == TextureMapSubLayer::PixelEditor)
        add("* Toggle Edit / Preview", MenuTogglePanelOrView);
    else if (m_subLayer == TextureMapSubLayer::CommitSave)
        add(m_runtimePanelVisible
            ? "* Hide Panel" : "* Show Authoring Panel",
            MenuTogglePanelOrView);
    add("* Back to Layer 2", MenuBack);
    return menu;
}

bool TextureMap2DWorkspace::handleMenuCommand(
    int command, ::WorkspaceServices& services) {
	const auto dispatch = [&](WorkspaceInputAction action) {
		const bool handled = handleLayer3Input({ action }, services);
		if (handled) updateCameraIntent(services);
		return handled;
	};
    if (command == MenuGatePrimary)
		return dispatch(WorkspaceInputAction::Activate);
    if (command == MenuTogglePanelOrView)
		return dispatch(WorkspaceInputAction::Toggle);
    if (command == MenuBack)
		return dispatch(WorkspaceInputAction::Back);
    if (command >= MenuRowBase &&
        command < MenuRowBase + runtimeRowCount()) {
        m_runtimeRow = command - MenuRowBase;
		return dispatch(WorkspaceInputAction::Activate);
    }
    return false;
}

TextureMap2DWorkspace::HostRequest
TextureMap2DWorkspace::takeHostRequest() {
    const HostRequest request = m_pendingHostRequest;
    m_pendingHostRequest = HostRequest{};
    return request;
}

void TextureMap2DWorkspace::replaceOutputCatalog(
    std::vector<StaticAssetCatalogEntry> catalog) {
    m_outputCatalog = std::move(catalog);
    m_outputCatalogReady = true;
    if (m_selectedOutputIndex >= m_outputCatalog.size())
        m_selectedOutputIndex = 0;
    m_layerStatusMessage.clear();
    m_pendingHostRequest = HostRequest{};
    m_pendingHostRequest.type =
        HostRequestType::RefreshBaseMaterialCatalog;
}

void TextureMap2DWorkspace::completeTargetLoad(
    bool loaded, bool ready, AssetId assetId,
    const std::string& displayName,
    const std::string& message) {
    m_loadedTargetDisplayName = loaded ? displayName : "";
    m_layerStatusMessage = message;
    if (!loaded) {
        m_target = TextureMapTargetContext{};
        m_session.clear();
        return;
    }
    const bool activated = activateLoadedTarget(assetId);
    if (!activated || !ready)
        m_target.readiness = ready
            ? TextureTargetReadiness::Invalid
            : m_target.readiness;
    m_planeProgress = 1.0f;
    m_planeTarget = 0.0f;
    m_planeAnimating = true;
    m_targetCameraCenterPending = true;
}

bool TextureMap2DWorkspace::completeTextEntry(
    const std::string& text, bool saveAsName,
    std::string* diagnostic) {
    if (saveAsName) {
        if (!validateNewSurfaceTargetName(text, diagnostic))
            return false;
        return true;
    }
    return completeSurfaceTargetName(text, diagnostic);
}

void TextureMap2DWorkspace::completeSaveRequest(
    bool succeeded, AssetId adoptedAssetId,
    const std::string& message) {
    m_runtimeStatusMessage = message;
    if (!succeeded || adoptedAssetId == INVALID_ASSET_ID) return;
    adoptSavedTarget(adoptedAssetId);
}

} // namespace vitru
