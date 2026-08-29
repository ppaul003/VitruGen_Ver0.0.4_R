#include "renderer_Euclid.h"
#include "TextureMap2DWorkspace.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

using namespace std;

namespace {
	WorkspacePanelRow makeRow(
		const string& label,
		const string& value,
		bool selected,
		bool selectable = true) {

		WorkspacePanelRow row;
		row.label = label;
		row.value = value;
		row.selectable = selectable;
		row.selected = selected;
		return row;
	}

	template <typename Enum>
	Enum cycleEnum(Enum value, int count, int direction) {
		const int step = direction < 0 ? -1 : 1;
		const int current = static_cast<int>(value);
		return static_cast<Enum>((current + step + count) % count);
	}
}

TextureMap2DWorkspace::TextureMap2DWorkspace() = default;
TextureMap2DWorkspace::~TextureMap2DWorkspace() {
}

bool TextureMap2DWorkspace::initialize(WorkspaceServices& services) {
	m_arbiter = services.arbiter;
	return services.renderer != nullptr && m_arbiter != nullptr;
}

void TextureMap2DWorkspace::enter(WorkspaceServices& services) {
	if (!m_arbiter) m_arbiter = services.arbiter;
	if (m_arbiter && m_arbiter->isDomainSelection()) {
		m_pendingHostRequest = HostRequest{};
		m_pendingHostRequest.type = HostRequestType::RefreshOutputCatalog;
	}
}

void TextureMap2DWorkspace::exit(WorkspaceServices& services) {
	(void)services;
}

void TextureMap2DWorkspace::update(
	const WorkspaceFrameContext& frame,
	WorkspaceServices& services) {

	(void)services;
	if (!m_targetSweepActive) return;

	m_planeProgress = std::max(
		0.0f,
		m_planeProgress - kTargetSweepSpeed * std::max(0.0f, frame.deltaTime));

	if (m_planeProgress <= 0.0f) {
		m_planeProgress = 0.0f;
		m_targetSweepActive = false;
	}
}

void TextureMap2DWorkspace::render(
	const WorkspaceFrameContext& frame,
	WorkspaceServices& services) {

	(void)frame;
	if (!services.renderer || !m_arbiter) return;

	EuclidRenderer& renderer = *services.renderer;
	if (m_arbiter->isWorkspaceConfiguration()) {
		if (m_targetLoaded) {
			renderer.drawStaticParticleTargetPreview(
				m_previewParticleRadius,
				m_layer2Item == Layer2Item::PreviewRadius);
		}
		return;
	}

	EuclidRenderer::UniformGrid grid;
	const float boxSize = static_cast<float>(renderer.getSimBoxSize());
	const float halfBox = boxSize * 0.5f;
	grid.dimensions = glm::ivec3(64);
	grid.origin = glm::vec3(-halfBox);
	grid.cellSize = glm::vec3(boxSize / 64.0f);
	grid.majorEvery = 8;

	EuclidRenderer::GridDisplay display;
	display.boundary = true;
	display.majorGrid = false;
	display.minorGrid = false;
	display.axes = false;

	const float planeZ = grid.origin.z + boxSize * m_planeProgress;
	renderer.drawUniformGridZRange(
		grid,
		planeZ,
		grid.origin.z + boxSize,
		display
	);

	if (m_targetSweepActive || m_planeProgress > 0.0f) {

		renderer.drawGridPlane(
			grid,
			EuclidRenderer::GridPlane::PLANE_XY,
			planeZ,
			false
		);
	}
	
}

bool TextureMap2DWorkspace::handleInput(
	const WorkspaceInputEvent& input,
	WorkspaceServices& services) {

	if (!m_arbiter) m_arbiter = services.arbiter;
	if (!m_arbiter) return false;
	if (m_targetSweepActive) return true;

	if (input.action == WorkspaceInputAction::Back) {

		if (m_arbiter->isWorkspaceConfiguration()) {
			m_arbiter->requestReturnToDomainSelection(
				TheArbiter::WorkspaceDomain::GRID_2D,
				TheArbiter::WorkspaceId::TEXTURE_MAP_2D);
		}
		else if (m_arbiter->isDomainSelection()) {
			m_arbiter->requestReturnToGlobalShell(
				TheArbiter::WorkspaceDomain::GRID_2D);
		}
		return true;
	}

	if (m_arbiter->isWorkspaceConfiguration()) {
		switch (input.action) {
		case WorkspaceInputAction::Previous:
			m_layer2Item = cycleEnum(m_layer2Item,
				static_cast<int>(Layer2Item::Count), -1); return true;
		case WorkspaceInputAction::Next:
			m_layer2Item = cycleEnum(m_layer2Item,
				static_cast<int>(Layer2Item::Count), +1); return true;
		case WorkspaceInputAction::Decrease:
			adjustLayer2Value(-1); return true;
		case WorkspaceInputAction::Increase:
			adjustLayer2Value(+1); return true;

		case WorkspaceInputAction::Activate:
			if (m_layer2Item == Layer2Item::RunWorkspaceEdit) {
				m_statusMessage =
					"TEXTURE_MAP_2D WORKSPACE EDIT IS DEFERRED IN THIS CHECKPOINT.";
			}
			return true;
		default:
			return false;
		}
	}

	if (!m_arbiter->isDomainSelection()) return false;
	switch (input.action) {
	case WorkspaceInputAction::Previous: moveLayer1Cursor(-1); return true;
	case WorkspaceInputAction::Next: moveLayer1Cursor(+1); return true;
	case WorkspaceInputAction::Decrease: adjustLayer1Value(-1, services); return true;
	case WorkspaceInputAction::Increase: adjustLayer1Value(+1, services); return true;
	case WorkspaceInputAction::Activate: activateLayer1(services); return true;
	default: return false;
	}
}

WorkspacePresentation TextureMap2DWorkspace::buildPresentation() const {
	if (m_arbiter && m_arbiter->isWorkspaceConfiguration())
		return buildLayer2Presentation();
	return buildLayer1Presentation();
}

WorkspacePresentation TextureMap2DWorkspace::buildLayer1TransitionPresentation() const {
	return buildLayer1Presentation();
}

WorkspacePresentation TextureMap2DWorkspace::buildLayer1Presentation() const {

	WorkspacePresentation p;
	p.panelVisible = true;

	p.workspaceName = "LAYER 1 -> GRID_2D WORKSPACE CONFIGURATION";
	p.layerLabel = "MODE: TEXTURE_MAP_2D";

	WorkspacePanelSection section;

	// =========================================================
	// [1] WORKSPACE SELECTION
	//
	// TextureMap2DWorkspace no longer owns an internal
	// GRAPH_2D / TEXTURE_MAP_2D selector enum.
	//
	// If A/D changes this row to GRAPH_2D, Arbiter/Tesseract
	// should physically switch cartridges.
	// =========================================================
	section.rows.push_back(
		makeRow(
			"[1]: GRID_2D SELECTION", 
			"TEXTURE_MAP_2D",
			m_layer1Item == Layer1Item::Workspace
		)
	);

	// =========================================================
	// [2] TARGET STATIC PARTICLE
	// =========================================================
	const vitru::StaticAssetCatalogEntry* target =
		selectedTarget();

	string targetName =
		"NO OUTPUT ASSETS";

	string badge =
		m_catalogReady
		? "NO OUTPUT ASSETS"
		: "CATALOG PENDING";


	if (target) {

		targetName = target->displayName;

		if (m_targetLoaded && targetName == m_loadedTargetName) {

			badge =
				m_targetReady
				? "LOADED | READY"
				: "LOADED | NOT READY";
		}
		else {

			badge =
				target->valid
				? "SELECTED | READY"
				: "SELECTED | INVALID";
		}
	}


	section.rows.push_back(
		makeRow(
			"[2]: TARGET SP",
			targetName +
			" [ " + badge + " ]",
			m_layer1Item == Layer1Item::Target
		)
	);


	// =========================================================
	// [3] LOAD TARGET
	// =========================================================
	section.rows.push_back(
		makeRow(
			"[3]: LOAD TARGET ASSET",
			"",
			m_layer1Item == Layer1Item::LoadTarget
		)
	);


	// =========================================================
	// [4] CONFIGURE
	// =========================================================
	section.rows.push_back(
		makeRow(
			"[4]: PRESS E TO CONFIGURE SIM",
			"",
			m_layer1Item == Layer1Item::Configure
		)
	);


	p.sections.push_back(section);


	// =========================================================
	// STATUS
	// =========================================================

	if (m_targetLoaded && m_targetReady) {

		p.statusLine =
			"TARGET LOADED: " +
			m_loadedTargetName +
			"\n"
			"TEXTURE_MAP_2D CONFIGURATION READY.";

		p.statusTone = WorkspaceStatusTone::Ready;
	}
	else if (!m_statusMessage.empty()) {
		p.statusLine = m_statusMessage;
		p.statusTone = WorkspaceStatusTone::Warning;
	}
	else if (!m_catalogReady) {
		p.statusLine = "OUTPUT CATALOG IS NOT INITIALIZED.";
		p.statusTone =WorkspaceStatusTone::Warning;
	}
	else if (m_outputCatalog.empty()) {

		p.statusLine = "NO OUTPUT STATIC PARTICLE ASSETS FOUND.";
		p.statusTone = WorkspaceStatusTone::Warning;
	}
	else if (target) {

		if (target->valid) {

			p.statusLine =
				"SELECTED TARGET: " +
				target->displayName +
				"\n"
				"LOAD TARGET ASSET WITH ROW [3].";

			p.statusTone = WorkspaceStatusTone::Caution;
		}
		else {

			p.statusLine =
				"SELECTED TARGET INVALID: " +
				target->displayName +
				"\n"
				"CONFIGURATION LOCKED.";

			p.statusTone = WorkspaceStatusTone::Warning;
		}
	}

	// =========================================================
	// FOOTER
	// =========================================================
	p.footerLine1 =
		"W / S: Select list     "
		"A / D: Change value";

	p.footerLine2 =
		"E: Activate selected row     "
		"Q: Back one layer";

	return p;
}

WorkspacePresentation TextureMap2DWorkspace::buildLayer2Presentation() const {
	WorkspacePresentation p;
	p.panelVisible = true;
	p.workspaceName = "LAYER 2 -> TEXTURE_MAP_2D TARGET CONFIGURATION";
	p.layerLabel = "MODE: TEXTURE_MAP_2D";

	WorkspacePanelSection target;
	target.heading = "TARGET SP:";

	target.rows.push_back(
		makeRow(
			"{ " + (m_loadedTargetName.empty() 
				? string("NO TARGET") 
				: m_loadedTargetName) 
			+ " }",
			m_targetReady 
				? "[ LOADED | READY ]"
				: "[ UNAVAILABLE ]",
			false,
			false
		)
	);

	target.notes.push_back("3D PREVIEW:");
	target.notes.push_back("{ TEXTURED STATIC PARTICLE }");
	p.sections.push_back(target);

	WorkspacePanelSection config;
	char radius[32];
	snprintf(radius, sizeof(radius), "%.4f", m_previewParticleRadius);
	config.rows.push_back(makeRow(
		"[1]: PREVIEW PARTICLE RADIUS", radius,
		m_layer2Item == Layer2Item::PreviewRadius));
	const string grid = std::to_string(m_pixelGridDivisions) + " x " +
		to_string(m_pixelGridDivisions);
	config.rows.push_back(makeRow(
		"[2]: PIXEL GRID", grid,
		m_layer2Item == Layer2Item::PixelGrid));
	config.rows.push_back(makeRow(
		"[3]: RUN WORKSPACE EDIT", "",
		m_layer2Item == Layer2Item::RunWorkspaceEdit));
	p.sections.push_back(config);

	if (!m_statusMessage.empty() &&
		m_statusMessage.find("DEFERRED") != string::npos) {
		p.statusLine = m_statusMessage;
		p.statusTone = WorkspaceStatusTone::Warning;
	}
	p.footerLine1 = "Q: TARGET SELECTION     W/S: SELECT     A/D: CHANGE VALUE";
	p.footerLine2 = "E: ACTIVATE     MOUSE: ORBIT / ZOOM PREVIEW";
	return p;
}

void TextureMap2DWorkspace::moveLayer1Cursor(int direction) {
	if (direction == 0) return;

	m_layer1Item = cycleEnum(
		m_layer1Item,
		static_cast<int>(Layer1Item::Count),
		direction
	);
}

void TextureMap2DWorkspace::adjustLayer1Value(
	int direction,
	WorkspaceServices& services) {

	if (direction == 0) return;


	// =====================================================
	// [1] SWITCH WORKSPACE
	// =====================================================
	if (m_layer1Item == Layer1Item::Workspace) {

		if (services.arbiter)
			services.arbiter->setActiveWorkspace(TheArbiter::WorkspaceId::GRAPH_2D);

		return;
	}


	// =====================================================
	// [2] TARGET CATALOG
	// =====================================================
	if (m_layer1Item == Layer1Item::Target && !m_outputCatalog.empty()) {

		const int count =
			static_cast<int>(m_outputCatalog.size());

		const int step =
			direction < 0
			? -1
			: 1;

		m_selectedTargetIndex =
			static_cast<size_t>(
				(static_cast<int>(m_selectedTargetIndex) + 
					step + count) % count);

		invalidateTarget();
	}
}

void TextureMap2DWorkspace::activateLayer1(WorkspaceServices& services) {
	// =====================================================
	// [2] TARGET
	//
	// E does not need to do anything here unless GOLD
	// explicitly gives it activation behavior.
	// =====================================================
	if (m_layer1Item == Layer1Item::Target) return;

	// =====================================================
	// [3] LOAD TARGET
	// =====================================================
	if (m_layer1Item == Layer1Item::LoadTarget) {

		const vitru::StaticAssetCatalogEntry* target = selectedTarget();

		if (!target || !target->valid) {
			m_statusMessage = target
				? "SELECTED STATIC PARTICLE MANIFEST IS INVALID."
				: "NO OUTPUT STATIC PARTICLE ASSETS FOUND.";
			return;
		}

		m_pendingHostRequest = HostRequest{};
		m_pendingHostRequest.type = HostRequestType::LoadTarget;
		m_pendingHostRequest.target = *target;
		m_pendingHostRequest.replaceAssetId = m_targetAssetId;
		m_statusMessage = "LOADING TARGET: " +
			target->displayName +
			"...";

		return;
	}

	// =====================================================
    // [4] CONFIGURE
    // =====================================================
	if (m_layer1Item == Layer1Item::Configure) {
		if (!m_targetLoaded || !m_targetReady || m_targetSweepActive) {
			m_statusMessage = 
				"TARGET CONFIGURATION LOCKED: "
				"\n"
				"LOAD A READY TARGET FIRST.";
			return;
		}
		if (services.arbiter) {
			services.arbiter->requestEnterWorkspaceConfiguration(
				TheArbiter::WorkspaceDomain::GRID_2D,
				TheArbiter::WorkspaceId::TEXTURE_MAP_2D);
		}
	}
}

void TextureMap2DWorkspace::adjustLayer2Value(int direction) {
	if (direction == 0) return;
	if (m_layer2Item == Layer2Item::PreviewRadius) {
		static constexpr std::array<float, 17> presets{
			0.0039f, 0.0046f, 0.0054f, 0.0061f, 0.0068f, 0.0076f,
			0.0083f, 0.0091f, 0.0098f, 0.0105f, 0.0113f, 0.0120f,
			0.0127f, 0.0135f, 0.0142f, 0.0149f, 0.0156f };
		size_t index = 0;
		for (size_t i = 1; i < presets.size(); ++i)
			if (fabs(presets[i] - m_previewParticleRadius) <
				fabs(presets[index] - m_previewParticleRadius)) index = i;
		if (direction < 0 && index > 0) --index;
		if (direction > 0 && index + 1 < presets.size()) ++index;
		m_previewParticleRadius = presets[index];
	}
	else if (m_layer2Item == Layer2Item::PixelGrid) {
		static constexpr array<uint32_t, 3> presets{ 32u, 64u, 128u };
		size_t index = m_pixelGridDivisions == 32u ? 0u
			: m_pixelGridDivisions == 128u ? 2u : 1u;
		if (direction < 0 && index > 0) --index;
		if (direction > 0 && index + 1 < presets.size()) ++index;
		m_pixelGridDivisions = presets[index];
	}
}

const vitru::StaticAssetCatalogEntry* TextureMap2DWorkspace::selectedTarget() const {
	return m_selectedTargetIndex < m_outputCatalog.size()
		? &m_outputCatalog[m_selectedTargetIndex] : nullptr;
}

void TextureMap2DWorkspace::invalidateTarget() {
	m_targetLoaded = false;
	m_targetReady = false;
	m_targetAssetId = vitru::INVALID_ASSET_ID;
	m_loadedTargetName.clear();
	m_statusMessage.clear();
	m_targetSweepActive = false;
	m_planeProgress = 1.0f;
}

TextureMap2DWorkspace::HostRequest TextureMap2DWorkspace::takeHostRequest() {
	const HostRequest request = m_pendingHostRequest;
	m_pendingHostRequest = HostRequest{};
	return request;
}

void TextureMap2DWorkspace::replaceOutputCatalog(
	std::vector<vitru::StaticAssetCatalogEntry> catalog) {

	m_outputCatalog = std::move(catalog);
	m_catalogReady = true;
	if (m_selectedTargetIndex >= m_outputCatalog.size()) m_selectedTargetIndex = 0;
	m_statusMessage.clear();
}

void TextureMap2DWorkspace::completeTargetLoad(
	bool loaded,
	bool ready,
	vitru::AssetId assetId,
	const string& displayName,
	const string& message) {

	m_targetLoaded = loaded;
	m_targetReady = loaded && ready;
	m_targetAssetId = loaded ? assetId : vitru::INVALID_ASSET_ID;
	m_loadedTargetName = loaded ? displayName : string{};
	m_statusMessage = message;

	if (loaded) {
		m_planeProgress = 1.0f;
		m_targetSweepActive = true;
	}
}
