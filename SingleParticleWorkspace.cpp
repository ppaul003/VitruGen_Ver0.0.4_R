#include "SingleParticleWorkspace.h"

#include <GL/glew.h>
#include <GL/freeglut.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "Camera.h"
#include "TheArbiter.h"
#include "kernel.h"
#include "marchingCubes.h"
#include "particleSystem.h"
#include "renderer_Euclid.h"

using namespace std;
using namespace glm;

namespace {
	template <typename Enum>
	Enum cycleEnum(Enum current, int count, int direction) {
		if (count <= 0 || direction == 0) return current;
		const int value = static_cast<int>(current);
		const int step = direction < 0 ? -1 : 1;
		return static_cast<Enum>((value + step + count) % count);
	}

	WorkspacePanelRow makeRow(
		const std::string& label,
		const std::string& value,
		bool selected,
		bool subordinate = false) {

		WorkspacePanelRow row;
		row.label = label;
		row.value = value;
		row.selectable = true;
		row.selected = selected;
		row.subordinate = subordinate;
		return row;
	}

	struct InjectionDirection { int x; int y; int z; };
	constexpr InjectionDirection kInjectionDirections[] = {
		{ 0, 0, 0 },
		{ 1, 0, 0 }, { 0, 1, 0 }, { -1, 0, 0 },
		{ 0, 0, 1 }, { 0, -1, 0 }, { 0, 0, -1 },
		{ 0, 1, -1 }, { 1, 1, 0 }, { 0, 1, 1 },
		{ -1, 1, 0 }, { 1, -1, 0 }, { 0, -1, -1 },
		{ -1, -1, 0 }, { 0, -1, 1 }, { -1, 0, -1 },
		{ 1, 0, -1 }, { -1, 0, 1 }, { 1, 0, 1 },
		{ 1, -1, 1 }, { -1, 1, -1 }, { 1, 1, -1 },
		{ -1, -1, 1 }, { 1, -1, -1 }, { -1, 1, 1 },
		{ 1, 1, 1 }, { -1, -1, -1 }
	};

	enum WorkspaceMenuCommand {
		MenuPrimary = 1,
		MenuTogglePanel,
		MenuBack,
		MenuLoad,
		MenuSave,
		MenuSaveAs,
		MenuExportObj,
		MenuToReference,
		MenuToShape,
		MenuToVolume,
		MenuToPreview,
		MenuToEdit,
		MenuToOffset,
		MenuToApply,
		MenuRunMarchingCubes,
		MenuCommit,
		MenuCollisionBase = 100,
		MenuRenderSourceBase = 120,
		MenuMeshBoundBase = 130,
		MenuDisplayBase = 140,
		MenuCageBase = 150,
		MenuPrimitiveBase = 200,
		MenuScaleModeBase = 220,
		MenuRotationModeBase = 230,
		MenuResetScale = 240,
		MenuResetRotation,
		MenuOffsetAxisBase = 250,
		MenuResetOffset = 260,
		MenuToggleInjectionMode,
		MenuCommitBrushBase,
		MenuMirrorBase = 270
	};
}

SingleParticleWorkspace::SingleParticleWorkspace() = default;
SingleParticleWorkspace::~SingleParticleWorkspace() {
	releaseCadResources();
}

void SingleParticleWorkspace::shutdown() {
	releaseCadResources();
	m_particleSystem.reset();
	m_singleParticleSystem = nullptr;
	m_singleParticleRadii = nullptr;
	m_renderer = nullptr;
	m_services = nullptr;
	m_entered = false;
	m_initialized = false;
}

void SingleParticleWorkspace::activateLoadedStaticParticleBase(
	bool editableVolumeRestored) {
	m_authoringSource = AuthoringSource::LoadedStaticMesh;
	m_loadedStaticMeshHasEditableVolume = editableVolumeRestored;
	m_particleRenderMode = ParticleRenderMode::Mesh;
	resetVolumeState(m_volume0State, VOLUME_PRIMITIVE_BASE);
	resetVolumeState(m_volume1State, VOLUME_PRIMITIVE_SPHERE);
	m_assemblyNode = AssemblyNode::Preview;
	m_injectionVoxel = InjectionVoxel::None;
	m_editTarget = EditTarget::Volume0;
	m_injectionMode = InjectionMode::Fuse;
	m_mirrorMode = MirrorMode::None;
	m_objectEditMode = ObjectEditMode::ScaleWhole;
	m_objectRotationMode = ObjectRotationMode::Pitch;
	m_objectTransformMode = ObjectTransformMode::Scale;
	m_offsetVector = OffsetVector::X;
	m_injectionRailT = 0.0f;
	markVolumeDirty();
}

bool SingleParticleWorkspace::initialize(WorkspaceServices& services) {
	if (m_initialized) return true;
	if (!services.renderer || !services.arbiter) return false;

	const uint3 grid = make_uint3(1u, 1u, 1u);
	m_particleRadii.assign(1u, m_particleRadius);
	m_particleSystem = std::make_unique<ParticleSystem>(1u, grid, true);
	m_singleParticleSystem = m_particleSystem.get();
	m_singleParticleRadii = &m_particleRadii;
	m_renderer = services.renderer;
	m_particleSystem->setUniformParticleColor(1.0f, 0.0f, 0.0f, 1.0f);
	m_particleSystem->reset(ParticleSystem::CNFG_DEFAULT_RESTART);

	if (!initializeCadResources()) {
		std::printf(
			"[SingleParticleWorkspace] WARNING: CAD resources are unavailable.\n");
	}

	m_services = &services;
	m_initialized = true;
	return true;
}

void SingleParticleWorkspace::enter(WorkspaceServices& services) {
	m_services = &services;
	m_entered = true;

	if (services.arbiter) {
		services.arbiter->setWorkspaceDomain(TheArbiter::WorkspaceDomain::GRID_3D);
		services.arbiter->setActiveWorkspace(
			TheArbiter::WorkspaceId::SINGLE_PARTICLE_MCAD);
	}

	updateCameraIntent(services);
	syncParticleRendering(services);
}

void SingleParticleWorkspace::exit(WorkspaceServices& services) {
	m_entered = false;
	if (services.renderer) services.renderer->setParticleHighlighted(false);
}

void SingleParticleWorkspace::update(
	const WorkspaceFrameContext& frame,
	WorkspaceServices& services) {

	m_lastFrame = frame;
	if (m_anchorPlaced) syncParticleRendering(services);
}

void SingleParticleWorkspace::render(
	const WorkspaceFrameContext& frame, 
	WorkspaceServices& services) {

	if (!services.renderer || !frame.displayEnabled) return;

	EuclidRenderer& renderer = *services.renderer;
	
	const bool layer1 = services.arbiter &&
		services.arbiter->isDomainSelection();

	const bool layer2 = services.arbiter &&
		services.arbiter->isWorkspaceConfiguration();

	const bool referenceOrShape = services.arbiter &&
		services.arbiter->isActiveWorkspace() &&
		(m_subLayer == SubLayer::Reference || m_subLayer == SubLayer::ShapeEdit);

	// ---------------------------------------------------------
	// LAYER 1
	//
	// GOLD displays the complete GRID_3D domain before the
	// SINGLE_PARTICLE anchor workspace becomes active.
	// ---------------------------------------------------------
	if (layer1) {
		configureWorkspaceGrid(services);
		renderer.displayGrid();

		return;
	}

	if (services.arbiter && services.arbiter->isActiveWorkspace() &&
		(m_subLayer == SubLayer::VolumeRender ||
			m_subLayer == SubLayer::MarchingCubes)) {

		if (initializePixelBuffer(frame.viewportWidth, frame.viewportHeight)) {
			const int method = m_subLayer == SubLayer::MarchingCubes ? 3 : 2;
			renderSPVolumeToPBO(
				*this,
				method,
				frame.viewportWidth,
				frame.viewportHeight,
				frame.thetaRad,
				frame.phiRad,
				frame.volumeRenderZs,
				0.0f,
				0.0f);
			renderSPVolumeTexture();
		}

		if (m_subLayer == SubLayer::VolumeRender) {
			renderSPVolumeInjectionVoxelPreview(
				*this, frame.thetaRad, frame.phiRad, frame.volumeRenderZs);
			renderSPVolumeInjectionEditTargetPreview(
				*this, frame.thetaRad, frame.phiRad, frame.volumeRenderZs);
			if (m_assemblyNode == AssemblyNode::OffsetObject)
				renderSPVolumeOffsetGrid(
					*this, frame.thetaRad, frame.phiRad, frame.volumeRenderZs);
		}
		else if (m_marchingCubes && m_marchingCubes->hasTriangleData()) {
			m_marchingCubes->renderWireframe(
				frame.thetaRad, frame.phiRad, frame.volumeRenderZs);
		}

		renderSPVolumeOrientationAxes(*this, frame.thetaRad, frame.phiRad);
		return;
	}

	if (!m_anchorPlaced || (!layer2 && !referenceOrShape)) return;

	const bool showWorkplane =
		m_subLayer == SubLayer::Reference &&
		m_selectionArmed && !m_selectedParticle && !m_subLayerPanelOpen;

	const bool useMesh = m_particleRenderMode == ParticleRenderMode::Mesh;
	const bool fillBounds = m_meshBoundMode == MeshBoundMode::Fill;
	const bool wireframe = m_displayMode == DisplayMode::Wireframe;
	const bool showCollision = m_displayMode == DisplayMode::RenderAndCollision;
	const bool showCage = m_subLayer == SubLayer::ShapeEdit && m_renderCageVisible;

	renderer.setParticleHighlighted(
		m_selectedParticle &&
		(m_subLayer == SubLayer::Reference || m_subLayer == SubLayer::ShapeEdit));

	renderer.displayParticleWorkspace(
		frame.thetaRad,
		frame.phiRad,
		frame.particleWorkspaceZs,
		m_workplaneSlice,
		showWorkplane,
		m_selectedParticle,
		m_hoverValid,
		m_hoverX,
		m_hoverY,
		useMesh,
		fillBounds,
		wireframe,
		showCollision,
		showCage);
}

bool SingleParticleWorkspace::handleInput(
	const WorkspaceInputEvent& input,
	WorkspaceServices& services) {

	if (!services.arbiter) return false;

	if (input.action == WorkspaceInputAction::Back) {
		backOneLayer(services);
		return true;
	}

	if (services.arbiter->isDomainSelection())
		return handleLayer1Input(input, services);

	if (services.arbiter->isWorkspaceConfiguration())
		return handleLayer2Input(input, services);

	if (services.arbiter->isActiveWorkspace()) {

		const bool handled = handleLayer3Input(input, services);

		// ---------------------------------------------------------
		// Layer-3 input can change camera-relevant workspace state:
		//
		//     selection armed
		//     particle selected
		//     sub-layer panel visibility
		//     active sub-layer
		//     assembly mode
		//
		// Re-resolve the workspace camera after every successfully
		// handled Layer-3 event.
		//
		// This is the cartridge-local equivalent of GOLD's old
		// syncCameraBehaviorFromArbiter() pass.
		// ---------------------------------------------------------
		if (handled) updateCameraIntent(services);

		return handled;
	}

	return false;
}

bool SingleParticleWorkspace::handleLayer1Input(
	const WorkspaceInputEvent& input,
	WorkspaceServices& services) {

	switch (input.action) {
	case WorkspaceInputAction::Previous:
		moveLayer1Cursor(-1);
		return true;
	case WorkspaceInputAction::Next:
		moveLayer1Cursor(+1);
		return true;
	case WorkspaceInputAction::Decrease:
		adjustLayer1Value(-1);
		if (services.arbiter) {
			using Id = TheArbiter::WorkspaceId;
			const Id id = m_grid3DWorkspace == Grid3DWorkspace::Graph3D
				? Id::GRAPH_3D
				: m_grid3DWorkspace == Grid3DWorkspace::LinkedParticles
				? Id::LINKED_PARTICLES_MCAD
				: Id::SINGLE_PARTICLE_MCAD;
			services.arbiter->setActiveWorkspace(id);
		}
		return true;
	case WorkspaceInputAction::Increase:
		adjustLayer1Value(+1);
		if (services.arbiter) {
			using Id = TheArbiter::WorkspaceId;
			const Id id = m_grid3DWorkspace == Grid3DWorkspace::Graph3D
				? Id::GRAPH_3D
				: m_grid3DWorkspace == Grid3DWorkspace::LinkedParticles
				? Id::LINKED_PARTICLES_MCAD
				: Id::SINGLE_PARTICLE_MCAD;
			services.arbiter->setActiveWorkspace(id);
		}
		return true;
	case WorkspaceInputAction::Activate:
		activateLayer1(services);
		return true;
	default:
		return false;
	}
}

bool SingleParticleWorkspace::handleLayer2Input(
	const WorkspaceInputEvent& input,
	WorkspaceServices& services) {

	switch (input.action) {
	case WorkspaceInputAction::Previous:
		moveLayer2Cursor(-1);
		return true;
	case WorkspaceInputAction::Next:
		moveLayer2Cursor(+1);
		return true;
	case WorkspaceInputAction::Decrease:
		adjustLayer2Value(-1);
		placeAnchor();
		syncParticleRendering(services);
		return true;
	case WorkspaceInputAction::Increase:
		adjustLayer2Value(+1);
		placeAnchor();
		syncParticleRendering(services);
		return true;
	case WorkspaceInputAction::Activate:
		activateLayer2(services);
		return true;
	default:
		return false;
	}
}

bool SingleParticleWorkspace::handleLayer3Input(
	const WorkspaceInputEvent& input,
	WorkspaceServices& services) {

	if (input.action == WorkspaceInputAction::Toggle) {
		toggleSubLayerPanel();
		return true;
	}

	if (input.action == WorkspaceInputAction::PointerMove) {
		if (m_subLayer == SubLayer::Reference && m_selectionArmed &&
			!m_selectedParticle && !m_subLayerPanelOpen) {
			updateHover(input.x, input.y);
			return true;
		}
		return false;
	}

	if (input.action == WorkspaceInputAction::PointerDown &&
		input.button == GLUT_LEFT_BUTTON &&
		m_subLayer == SubLayer::Reference && m_selectionArmed &&
		!m_selectedParticle && !m_subLayerPanelOpen) {
		updateHover(input.x, input.y);
		return trySelectParticle(input.x, input.y);
	}

	if (m_subLayerPanelOpen && m_selectedParticle) {
		if (input.action == WorkspaceInputAction::Previous ||
			input.action == WorkspaceInputAction::Next) {
			const int count = activePanelItemCount();
			if (count > 0) {
				const int direction = input.action == WorkspaceInputAction::Previous
					? -1 : 1;
				for (int attempt = 0; attempt < count; ++attempt) {
					m_activePanelItem =
						(m_activePanelItem + direction + count) % count;
					const bool disabledApply =
						m_subLayer == SubLayer::VolumeRender &&
						m_assemblyNode == AssemblyNode::OffsetObject &&
						((hasInjectionVoxelSelected() && isEditingInjectionVoxel0() &&
							m_activePanelItem == 4) ||
							(!hasInjectionVoxelSelected() && m_activePanelItem == 2)) &&
						!canApplyVolumeToBase();
					if (!disabledApply) break;
				}
			}
			return true;
		}
		if (input.action == WorkspaceInputAction::Decrease ||
			input.action == WorkspaceInputAction::Increase) {
			adjustSubLayerPanelItem(
				input.action == WorkspaceInputAction::Decrease ? -1 : 1,
				services);
			return true;
		}
		if (input.action == WorkspaceInputAction::Activate) {
			activateSubLayerPanelItem(services);
			return true;
		}
	}

	if (input.action == WorkspaceInputAction::Activate) {
		if (m_subLayer == SubLayer::Reference) {
			handleReferencePrimaryAction();
			return true;
		}
		// GOLD deliberately does not auto-advance ShapeEdit with E.
		if (m_subLayer == SubLayer::ShapeEdit) return true;
		if (m_subLayer == SubLayer::VolumeRender) {
			if (m_assemblyNode == AssemblyNode::Preview)
				enterMarchingCubes(services);
			else if (m_assemblyNode == AssemblyNode::EditObject) {
				m_assemblyNode = AssemblyNode::OffsetObject;
				m_activePanelItem = 0;
			}
			else if (m_assemblyNode == AssemblyNode::OffsetObject) {
				if (canApplyVolumeToBase()) {
					m_assemblyNode = AssemblyNode::ApplyToBase;
					m_activePanelItem = 0;
				}
			}
			else if (commitSPWorkingVolume(*this)) {
				m_assemblyNode = AssemblyNode::Preview;
				m_activePanelItem = 0;
				m_injectionVoxel = InjectionVoxel::None;
				m_editTarget = EditTarget::Volume0;
				m_mirrorMode = MirrorMode::None;
				resetVolumeState(m_volume0State, VOLUME_PRIMITIVE_BASE);
				resetVolumeState(m_volume1State, VOLUME_PRIMITIVE_SPHERE);
				resetObjectOffset();
			}
			updateCameraIntent(services);
			return true;
		}
		if (m_subLayer == SubLayer::MarchingCubes) {
			m_subLayer = SubLayer::Reference;
			m_assemblyNode = AssemblyNode::Preview;
			m_subLayerPanelOpen = false;
			m_activePanelItem = 0;
			m_selectedParticle = false;
			m_hoverValid = false;
			m_workplaneSlice = 0;
			updateCameraIntent(services);
			return true;
		}
	}

	if ((input.action == WorkspaceInputAction::Previous ||
		input.action == WorkspaceInputAction::Next) &&
		m_subLayer == SubLayer::Reference && m_selectionArmed &&
		!m_selectedParticle && !m_subLayerPanelOpen) {
		const int delta = input.action == WorkspaceInputAction::Previous ? 1 : -1;
		m_workplaneSlice = std::max(-64, std::min(64, m_workplaneSlice + delta));
		if (std::abs(m_workplaneSlice) > 1) m_selectedParticle = false;
		return true;
	}

	if (handleVolumeDirectInput(input)) return true;

	return false;
}

void SingleParticleWorkspace::moveLayer1Cursor(int direction) {
	m_layer1Item = cycleEnum(
		m_layer1Item,
		static_cast<int>(Layer1Item::Count),
		direction);
}

void SingleParticleWorkspace::adjustLayer1Value(int direction) {
	if (m_layer1Item == Layer1Item::Workspace) {
		m_grid3DWorkspace = cycleEnum(
			m_grid3DWorkspace,
			static_cast<int>(Grid3DWorkspace::Count),
			direction);
	}
	else if (m_layer1Item == Layer1Item::ParticleType) {
		m_objectType = cycleEnum(
			m_objectType,
			static_cast<int>(ObjectType::Count),
			direction);
	}
}

void SingleParticleWorkspace::activateLayer1(WorkspaceServices& services) {
	if (m_layer1Item != Layer1Item::Configure ||
		m_grid3DWorkspace != Grid3DWorkspace::SingleParticle ||
		m_objectType != ObjectType::Static || !services.arbiter) return;

	services.arbiter->setActiveWorkspace(
		TheArbiter::WorkspaceId::SINGLE_PARTICLE_MCAD);
	services.arbiter->setApplicationLayer(
		TheArbiter::ApplicationLayer::WORKSPACE_CONFIGURATION);
	m_layer2Item = Layer2Item::Color;
	placeAnchor();
	syncParticleRendering(services);
	updateCameraIntent(services);
}

void SingleParticleWorkspace::moveLayer2Cursor(int direction) {
	m_layer2Item = cycleEnum(
		m_layer2Item,
		static_cast<int>(Layer2Item::Count),
		direction);
}

void SingleParticleWorkspace::adjustLayer2Value(int direction) {
	switch (m_layer2Item) {
	case Layer2Item::Color:
		m_particleColor = cycleEnum(
			m_particleColor,
			static_cast<int>(ParticleColor::Count),
			direction);
		break;
	case Layer2Item::Radius:
		m_particleRadius = std::max(
			kParticleRadiusMin,
			std::min(kParticleRadiusMax,
				m_particleRadius + (direction < 0 ? -kParticleRadiusStep : kParticleRadiusStep)));
		break;
	case Layer2Item::RenderMode:
		m_particleRenderMode = cycleEnum(
			m_particleRenderMode,
			static_cast<int>(ParticleRenderMode::Count),
			direction);
		break;
	default:
		break;
	}
}

void SingleParticleWorkspace::activateLayer2(WorkspaceServices& services) {
	if (m_layer2Item != Layer2Item::Run || !services.arbiter) return;

	services.arbiter->setApplicationLayer(
		TheArbiter::ApplicationLayer::ACTIVE_WORKSPACE);
	resetRuntimeTraversal();
	placeAnchor();
	syncParticleRendering(services);
	updateCameraIntent(services);
}

void SingleParticleWorkspace::backOneLayer(WorkspaceServices& services) {
	if (!services.arbiter) return;

	if (services.arbiter->isDomainSelection()) {
		
		services.arbiter->requestReturnToGlobalShell(
			TheArbiter::WorkspaceDomain::GRID_3D
		);

		return;
	}

	if (services.arbiter->isWorkspaceConfiguration()) {
		services.arbiter->setApplicationLayer(TheArbiter::ApplicationLayer::DOMAIN_SELECTION);
		m_anchorPlaced = false;

		if (services.renderer) services.renderer->setParticleHighlighted(false);
		updateCameraIntent(services);
		return;
	}

	if (!services.arbiter->isActiveWorkspace()) return;

	// =========================================================
	// SUB-LAYER 1 -> SUB-LAYER 0
	//
	// Q means structural sub-layer traversal.
	//
	// Keep:
	//     particle selected
	//     panel visible
	//
	// Only the panel CONTENT changes.
	// =========================================================

	// =========================================================
	// SUB-LAYER 0
	// =========================================================
	if (m_subLayer == SubLayer::Reference) {

		// -----------------------------------------------------
		// Panel currently open:
		//
		// Q only closes the panel.
		//
		// Particle remains selected/highlighted.
		// -----------------------------------------------------
		if (m_subLayerPanelOpen) {

			m_subLayerPanelOpen = false;
			m_activePanelItem = 0;

			updateCameraIntent(services);

			return;
		}

		// -----------------------------------------------------
		// Particle still selected/highlighted:
		//
		// Q is intentionally a no-op.
		// E owns deselection.
		// -----------------------------------------------------
		if (m_selectedParticle) return;
		
		// -----------------------------------------------------
		// XY workplane still armed:
		//
		// Q is intentionally a no-op.
		//
		// E owns workplane disengagement.
		// -----------------------------------------------------
		if (m_selectionArmed) return;

		// -----------------------------------------------------
		// TRUE Layer-3 runtime state:
		//
		//     no panel
		//     no selected particle
		//     no armed XY workplane
		//
		// NOW Q may structurally return to Layer 2.
		// -----------------------------------------------------
		services.arbiter->setApplicationLayer(TheArbiter::ApplicationLayer::WORKSPACE_CONFIGURATION);

		m_activePanelItem = 0;
		m_hoverValid = false;
		m_workplaneSlice = 0;

		updateCameraIntent(services);

		return;
	}

	if (m_subLayer == SubLayer::MarchingCubes) {
		m_subLayer = SubLayer::VolumeRender;
		m_assemblyNode = AssemblyNode::Preview;
		m_subLayerPanelOpen = true;
		m_activePanelItem = 0;
	}
	else if (m_subLayer == SubLayer::VolumeRender && m_assemblyNode != AssemblyNode::Preview) {
		m_assemblyNode = 
			static_cast<AssemblyNode>(static_cast<int>(m_assemblyNode) - 1);

		m_activePanelItem = 0;
	}
	else if (m_subLayer == SubLayer::VolumeRender) {
		m_subLayer = SubLayer::ShapeEdit;
		m_subLayerPanelOpen = true;
		m_activePanelItem = 0;
	}
	else if (m_subLayer == SubLayer::ShapeEdit) {
		m_subLayer = SubLayer::Reference;
		m_subLayerPanelOpen = true;
		m_activePanelItem = 0;
		updateCameraIntent(services);
	}
	else {
		services.arbiter->setApplicationLayer(
			TheArbiter::ApplicationLayer::WORKSPACE_CONFIGURATION);
		m_selectionArmed = false;
		m_selectedParticle = false;
		m_hoverValid = false;
		m_subLayerPanelOpen = false;
		m_workplaneSlice = 0;
		m_assemblyNode = AssemblyNode::Preview;
	}

	updateCameraIntent(services);
}

void SingleParticleWorkspace::placeAnchor() {
	if (!m_particleSystem) return;

	m_particleSystem->setParticleRadius(m_particleRadius);
	const float color[3][4] = {
		{ 1.0f, 0.0f, 0.0f, 1.0f },
		{ 0.0f, 0.25f, 1.0f, 1.0f },
		{ 0.0f, 1.0f, 0.25f, 1.0f }
	};
	const int colorIndex = static_cast<int>(m_particleColor);
	m_particleSystem->setUniformParticleColor(
		color[colorIndex][0], color[colorIndex][1],
		color[colorIndex][2], color[colorIndex][3]);

	float position[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	float velocity[4] = { 0.0f, 0.0f, 0.0f, m_particleRadius };
	float acceleration[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	m_particleSystem->setParticle(ParticleSystem::POSITION, 0, position);
	m_particleSystem->setParticle(ParticleSystem::VELOCITY, 0, velocity);
	m_particleSystem->setParticle(ParticleSystem::ACCELERATION, 0, acceleration);
	m_particleRadii[0] = m_particleRadius;
	m_anchorPlaced = true;
}

void SingleParticleWorkspace::syncParticleRendering(WorkspaceServices& services) {
	if (!services.renderer || !m_particleSystem || m_particleRadii.empty()) return;
	EuclidRenderer& renderer = *services.renderer;
	renderer.setParticleSystem(m_particleSystem.get());
	renderer.setParticleRadius(m_particleSystem->getParticleRadius());
	renderer.setColorBuffer(m_particleSystem->getColorBuffer());
	renderer.setRadiusBuffer(m_particleSystem->getRadiiBuffer());
	renderer.setVertexBuffer(m_particleSystem->getCurrentReadBuffer(), 1);
	renderer.setRadius(m_particleRadii.data(), 1);
}

void SingleParticleWorkspace::updateCameraIntent(WorkspaceServices& services) const {
	if (!services.camera || !services.arbiter) return;
	if (services.arbiter->isActiveWorkspace()) {
		if (m_subLayer == SubLayer::VolumeRender)
			services.camera->setBehaviorMode(CameraProcessor::CAM_SINGLE_PARTICLE_VOLUME);
		else if (m_subLayer == SubLayer::MarchingCubes)
			services.camera->setBehaviorMode(CameraProcessor::CAM_SINGLE_PARTICLE_MARCHING_CUBES);
		else if (m_subLayer == SubLayer::Reference && m_selectionArmed &&
			!m_selectedParticle && !m_subLayerPanelOpen)
			services.camera->setBehaviorMode(CameraProcessor::CAM_SINGLE_PARTICLE_WORKPLANE_LOCKED);
		else
			services.camera->setBehaviorMode(CameraProcessor::CAM_SINGLE_PARTICLE_ORBIT_CLOSE);
	}
	else {
		services.camera->setBehaviorMode(CameraProcessor::CAM_SINGLE_PARTICLE_ORBIT_CLOSE);
	}
}

void SingleParticleWorkspace::configureWorkspaceGrid(WorkspaceServices& services) const {
	if (!services.renderer) return;

	EuclidRenderer& renderer = *services.renderer;

	// ---------------------------------------------------------
	// GOLD Ver004 GRID_3D workspace domain.
	//
	// GOLD sourced this from the shared 64^3 ParticleSystem:
	//
	//     simulation box = 4.0
	//     grid           = 64 x 64 x 64
	//     origin         = (-2, -2, -2)
	//     cell size      = 4 / 64 = 0.0625
	//
	// The refactored cartridge declares the same generic
	// spatial presentation directly to EuclidRenderer.
	// ---------------------------------------------------------
	const int gridDim = renderer.getGridDimSize();
	const float boxSize = static_cast<float>(renderer.getSimBoxSize());

	if (gridDim <= 0 || boxSize <= 0.0f) return;

	const float halfBox = boxSize * 0.5f;

	const float cellSize =
		boxSize / static_cast<float>(gridDim);

	renderer.setGrid(
		ivec3(gridDim, gridDim, gridDim),
		vec3(-halfBox, -halfBox, -halfBox),
		vec3(cellSize, cellSize, cellSize)
	);

	renderer.setGridMode3D();
	renderer.setGridStyle(renderer.getGridMajorEvery(), false);

	renderer.setWorkspaceGridVisibility(
		true,	// boundary
		true,	// major grid
		false,	// minor grid
		true	// axes
	);

}

void SingleParticleWorkspace::resetRuntimeTraversal() {
	m_subLayer = SubLayer::Reference;
	m_activePanelItem = 0;
	m_selectionArmed = false;
	m_selectedParticle = false;
	m_hoverValid = false;
	m_hoverX = 0.0f;
	m_hoverY = 0.0f;
	m_workplaneSlice = 0;
	m_subLayerPanelOpen = false;
	m_assemblyNode = AssemblyNode::Preview;
}

void SingleParticleWorkspace::handleReferencePrimaryAction() {
	if (m_selectedParticle) {
		m_selectedParticle = false;
		m_selectionArmed = true;
		m_subLayerPanelOpen = false;
		m_activePanelItem = 0;
		return;
	}
	if (!m_selectionArmed) {
		m_selectionArmed = true;
		m_subLayerPanelOpen = false;
		m_activePanelItem = 0;
		return;
	}
	m_selectionArmed = false;
	m_hoverValid = false;
	m_hoverX = 0.0f;
	m_hoverY = 0.0f;
	m_subLayerPanelOpen = false;
	m_activePanelItem = 0;
}

void SingleParticleWorkspace::toggleSubLayerPanel() {
	if (!m_selectedParticle) {
		m_subLayerPanelOpen = false;
		m_activePanelItem = 0;
		return;
	}
	m_subLayerPanelOpen = !m_subLayerPanelOpen;
	if (m_subLayerPanelOpen) m_activePanelItem = 0;
}

int SingleParticleWorkspace::activePanelItemCount() const {
	if (m_subLayer == SubLayer::Reference) return 4;
	if (m_subLayer == SubLayer::ShapeEdit) return 6;
	if (m_subLayer == SubLayer::MarchingCubes) return 5;
	if (m_assemblyNode == AssemblyNode::Preview) return 3;
	if (m_assemblyNode == AssemblyNode::ApplyToBase) return 3;
	if (m_assemblyNode == AssemblyNode::EditObject) {
		if (!hasInjectionVoxelSelected()) return 4;
		return isEditingInjectionVoxel1() ? 4 : 5;
	}
	if (!hasInjectionVoxelSelected()) return 4;
	return isEditingInjectionVoxel1() ? 4 : 6;
}

void SingleParticleWorkspace::adjustSubLayerPanelItem(
	int direction,
	WorkspaceServices& services) {
	if (m_subLayer == SubLayer::Reference) {
		if (m_activePanelItem == 0)
			m_collisionShape = cycleEnum(m_collisionShape,
				static_cast<int>(CollisionShape::Count), direction);
		return;
	}
	if (m_subLayer == SubLayer::ShapeEdit) {
		switch (m_activePanelItem) {
		case 0:
			m_particleRenderMode = cycleEnum(m_particleRenderMode,
				static_cast<int>(ParticleRenderMode::Count), direction);
			syncParticleRendering(services);
			break;
		case 1:
			m_meshBoundMode = cycleEnum(m_meshBoundMode,
				static_cast<int>(MeshBoundMode::Count), direction);
			break;
		case 2:
			m_displayMode = cycleEnum(m_displayMode,
				static_cast<int>(DisplayMode::Count), direction);
			break;
		case 3: m_renderCageVisible = !m_renderCageVisible; break;
		default: break;
		}
		return;
	}
	if (m_subLayer != SubLayer::VolumeRender ||
		m_assemblyNode == AssemblyNode::ApplyToBase) return;
	if (m_assemblyNode == AssemblyNode::Preview) {
		if (m_activePanelItem == 0) cycleInjectionVoxelSelection(direction);
		return;
	}
	if (m_assemblyNode == AssemblyNode::EditObject) {
		if (hasInjectionVoxelSelected()) {
			switch (m_activePanelItem) {
			case 0: cycleVolumeEditTarget(direction); break;
			case 1: cycleVolumePrimitiveSelection(direction); break;
			case 2:
				if (isEditingInjectionVoxel0()) cycleRotationIncrement(direction);
				break;
			case 3:
				if (isEditingInjectionVoxel1()) cycleMirrorMode(direction);
				break;
			default: break;
			}
		}
		else if (m_activePanelItem == 0)
			cycleVolumePrimitiveSelection(direction);
		else if (m_activePanelItem == 1)
			cycleRotationIncrement(direction);
		return;
	}
	if (hasInjectionVoxelSelected()) {
		switch (m_activePanelItem) {
		case 0: cycleVolumeEditTarget(direction); break;
		case 1: cycleOffsetVector(direction); break;
		case 2: cycleOffsetIncrement(direction); break;
		case 3:
			if (isEditingInjectionVoxel0()) adjustInjectionRail(direction);
			else cycleVolumeInjectionMode(direction);
			break;
		default: break;
		}
	}
	else if (m_activePanelItem == 0) cycleOffsetVector(direction);
	else if (m_activePanelItem == 1) cycleOffsetIncrement(direction);
}

void SingleParticleWorkspace::activateSubLayerPanelItem(
	WorkspaceServices& services) {
	if (m_subLayer == SubLayer::Reference) {
		if (m_activePanelItem == 1)
			m_pendingHostRequest = HostRequest::LoadStaticParticle;
		else if (m_activePanelItem == 2)
			m_pendingHostRequest = HostRequest::SaveStaticParticleAs;
		else if (m_activePanelItem == 3) {
			m_subLayer = SubLayer::ShapeEdit;
			m_subLayerPanelOpen = true;
			m_activePanelItem = 0;
			updateCameraIntent(services);
		}
		return;
	}
	if (m_subLayer == SubLayer::ShapeEdit) {
		if (m_activePanelItem <= 3) {
			adjustSubLayerPanelItem(1, services);
		}
		else if (m_activePanelItem == 4) {
			m_subLayer = SubLayer::VolumeRender;
			m_assemblyNode = AssemblyNode::Preview;
			m_subLayerPanelOpen = true;
			m_activePanelItem = 0;
			markVolumeDirty();
			updateCameraIntent(services);
		}
		else {
			m_subLayer = SubLayer::Reference;
			m_subLayerPanelOpen = true;
			m_activePanelItem = 0;
			updateCameraIntent(services);
		}
		return;
	}
	if (m_subLayer == SubLayer::MarchingCubes) {
		switch (m_activePanelItem) {
		case 0: m_pendingHostRequest = HostRequest::SaveStaticParticle; break;
		case 1: m_pendingHostRequest = HostRequest::SaveStaticParticleAs; break;
		case 2: m_pendingHostRequest = HostRequest::ExportObj; break;
		case 3:
			m_subLayer = SubLayer::VolumeRender;
			m_assemblyNode = AssemblyNode::Preview;
			m_subLayerPanelOpen = true;
			m_activePanelItem = 0;
			break;
		case 4:
			m_subLayer = SubLayer::Reference;
			m_assemblyNode = AssemblyNode::Preview;
			m_subLayerPanelOpen = false;
			m_activePanelItem = 0;
			m_selectedParticle = false;
			m_hoverValid = false;
			m_workplaneSlice = 0;
			break;
		default: break;
		}
		updateCameraIntent(services);
		return;
	}

	switch (m_assemblyNode) {
	case AssemblyNode::Preview:
		if (m_activePanelItem == 1) {
			m_assemblyNode = AssemblyNode::EditObject;
			m_activePanelItem = 0;
			markVolumeDirty();
		}
		else if (m_activePanelItem == 2) enterMarchingCubes(services);
		break;
	case AssemblyNode::EditObject:
		if (hasInjectionVoxelSelected()) {
			if (m_activePanelItem == 0) cycleVolumeEditTarget(1);
			else if (isEditingInjectionVoxel0() && m_activePanelItem == 3) {
				m_assemblyNode = AssemblyNode::OffsetObject;
				m_activePanelItem = 0;
			}
			else if (isEditingInjectionVoxel0() && m_activePanelItem == 4) {
				m_assemblyNode = AssemblyNode::Preview;
				m_activePanelItem = 0;
				markVolumeDirty();
			}
			else if (isEditingInjectionVoxel1() && m_activePanelItem == 2)
				commitBrushBase();
			else if (isEditingInjectionVoxel1() && m_activePanelItem == 3)
				cycleMirrorMode(1);
		}
		else if (m_activePanelItem == 2) {
			m_assemblyNode = AssemblyNode::OffsetObject;
			m_activePanelItem = 0;
		}
		else if (m_activePanelItem == 3) {
			m_assemblyNode = AssemblyNode::Preview;
			m_activePanelItem = 0;
			markVolumeDirty();
		}
		break;
	case AssemblyNode::OffsetObject:
		if (hasInjectionVoxelSelected()) {
			if (m_activePanelItem == 0) cycleVolumeEditTarget(1);
			else if (isEditingInjectionVoxel1() && m_activePanelItem == 3)
				cycleVolumeInjectionMode(1);
			else if (isEditingInjectionVoxel0() && m_activePanelItem == 4 &&
				canApplyVolumeToBase()) {
				m_assemblyNode = AssemblyNode::ApplyToBase;
				m_activePanelItem = 0;
			}
			else if (isEditingInjectionVoxel0() && m_activePanelItem == 5) {
				m_assemblyNode = AssemblyNode::EditObject;
				m_activePanelItem = 0;
			}
		}
		else if (m_activePanelItem == 2 && canApplyVolumeToBase()) {
			m_assemblyNode = AssemblyNode::ApplyToBase;
			m_activePanelItem = 0;
		}
		else if (m_activePanelItem == 3) {
			m_assemblyNode = AssemblyNode::EditObject;
			m_activePanelItem = 0;
		}
		break;
	case AssemblyNode::ApplyToBase:
		if (m_activePanelItem == 0) {
			if (commitSPWorkingVolume(*this)) {
				m_assemblyNode = AssemblyNode::Preview;
				m_activePanelItem = 0;
				m_injectionVoxel = InjectionVoxel::None;
				m_editTarget = EditTarget::Volume0;
				m_mirrorMode = MirrorMode::None;
				resetVolumeState(m_volume0State, VOLUME_PRIMITIVE_BASE);
				resetVolumeState(m_volume1State, VOLUME_PRIMITIVE_SPHERE);
			}
		}
		else if (m_activePanelItem == 1) {
			m_assemblyNode = AssemblyNode::OffsetObject;
			m_activePanelItem = 0;
		}
		else {
			m_assemblyNode = AssemblyNode::Preview;
			m_activePanelItem = 0;
			markVolumeDirty();
		}
		break;
	default: break;
	}
}

bool SingleParticleWorkspace::handleVolumeDirectInput(
	const WorkspaceInputEvent& input) {
	if (m_subLayer != SubLayer::VolumeRender) return false;
	if (m_assemblyNode == AssemblyNode::EditObject) {
		switch (input.action) {
		case WorkspaceInputAction::Previous: adjustObjectScale(1); return true;
		case WorkspaceInputAction::Next: adjustObjectScale(-1); return true;
		case WorkspaceInputAction::Decrease: adjustObjectRotation(-1); return true;
		case WorkspaceInputAction::Increase: adjustObjectRotation(1); return true;
		case WorkspaceInputAction::Option0:
			m_objectTransformMode = ObjectTransformMode::Scale;
			resetObjectScale(); return true;
		case WorkspaceInputAction::Option1:
			m_objectEditMode = ObjectEditMode::ScaleWhole;
			m_objectTransformMode = ObjectTransformMode::Scale; return true;
		case WorkspaceInputAction::Option2:
			m_objectEditMode = ObjectEditMode::ScaleZ;
			m_objectTransformMode = ObjectTransformMode::Scale; return true;
		case WorkspaceInputAction::Option3:
			m_objectEditMode = ObjectEditMode::ScaleY;
			m_objectTransformMode = ObjectTransformMode::Scale; return true;
		case WorkspaceInputAction::Option4:
			m_objectEditMode = ObjectEditMode::ScaleX;
			m_objectTransformMode = ObjectTransformMode::Scale; return true;
		case WorkspaceInputAction::Option5:
			m_objectRotationMode = ObjectRotationMode::Pitch;
			m_objectTransformMode = ObjectTransformMode::Rotation; return true;
		case WorkspaceInputAction::Option6:
			m_objectRotationMode = ObjectRotationMode::Yaw;
			m_objectTransformMode = ObjectTransformMode::Rotation; return true;
		case WorkspaceInputAction::Option7:
			m_objectRotationMode = ObjectRotationMode::Roll;
			m_objectTransformMode = ObjectTransformMode::Rotation; return true;
		case WorkspaceInputAction::Option8:
			m_objectTransformMode = ObjectTransformMode::Rotation;
			resetObjectRotation(); return true;
		default: return false;
		}
	}
	if (m_assemblyNode == AssemblyNode::OffsetObject && !m_subLayerPanelOpen) {
		if (input.action == WorkspaceInputAction::Previous) {
			adjustObjectOffset(1); return true;
		}
		if (input.action == WorkspaceInputAction::Next) {
			adjustObjectOffset(-1); return true;
		}
	}
	return false;
}

void SingleParticleWorkspace::enterMarchingCubes(WorkspaceServices& services) {
	if (!extractMarchingCubesMesh()) return;
	m_subLayer = SubLayer::MarchingCubes;
	m_subLayerPanelOpen = true;
	m_activePanelItem = 0;
	updateCameraIntent(services);
}

bool SingleParticleWorkspace::trySelectParticle(int, int) {
	const bool sliceNearOrigin = std::abs(m_workplaneSlice) <= 1;
	const float pickRadius = 0.35f;
	const bool hoverNearOrigin = m_hoverValid &&
		(m_hoverX * m_hoverX + m_hoverY * m_hoverY) <= pickRadius * pickRadius;
	if (sliceNearOrigin && hoverNearOrigin) {
		m_selectedParticle = true;
		m_selectionArmed = true;
		m_subLayerPanelOpen = false;
		m_activePanelItem = 0;
	}
	return true;
}

void SingleParticleWorkspace::updateHover(int x, int y) {
	const int width = m_lastFrame.viewportWidth;
	const int height = m_lastFrame.viewportHeight;
	if (width <= 0 || height <= 0) return;
	const float nx = 2.0f * static_cast<float>(x) / static_cast<float>(width) - 1.0f;
	const float ny = 1.0f - 2.0f * static_cast<float>(y) / static_cast<float>(height);
	m_hoverX = nx * 2.0f;
	m_hoverY = ny * 2.0f;
	m_hoverValid = true;
}

bool SingleParticleWorkspace::initializeCadResources() {
	const std::size_t bytes =
		static_cast<std::size_t>(m_volumeSize.x) *
		static_cast<std::size_t>(m_volumeSize.y) *
		static_cast<std::size_t>(m_volumeSize.z) * sizeof(float);

	if (bytes == 0u) return false;

	auto allocateVolume = [bytes](float*& pointer) {
		if (pointer) return;
		allocateArray(reinterpret_cast<void**>(&pointer), bytes);
	};

	allocateVolume(m_dWorkingVolume);
	allocateVolume(m_dBaseVolume);
	allocateVolume(m_dBrushVolume);
	allocateVolume(m_dMirrorBrushVolume);

	if (!m_dWorkingVolume || !m_dBaseVolume ||
		!m_dBrushVolume || !m_dMirrorBrushVolume) return false;

	clearVolumeKernelLauncher(m_dWorkingVolume, m_volumeSize, 1.0e6f);
	clearVolumeKernelLauncher(m_dBaseVolume, m_volumeSize, 1.0e6f);
	clearVolumeKernelLauncher(m_dBrushVolume, m_volumeSize, 1.0e6f);
	clearVolumeKernelLauncher(m_dMirrorBrushVolume, m_volumeSize, 1.0e6f);
	threadSync();

	m_committedVolumeReady = true;
	m_hasCommittedGeometry = false;
	m_volumeDirty = true;

	if (!initializeSPVolumeBoundarySensor()) return false;

	m_marchingCubes = std::make_unique<MarchingCubes>();
	if (!m_marchingCubes->init(m_volumeSize)) {
		m_marchingCubes.reset();
		return false;
	}

	std::printf(
		"[SingleParticleWorkspace] CAD resources ready: %d x %d x %d.\n",
		m_volumeSize.x, m_volumeSize.y, m_volumeSize.z);
	return true;
}

void SingleParticleWorkspace::releaseCadResources() {
	releasePixelBuffer();
	releaseSPVolumeBoundarySensor();

	if (m_marchingCubes) {
		m_marchingCubes->shutdown();
		m_marchingCubes.reset();
	}

	auto releaseVolume = [](float*& pointer) {
		if (!pointer) return;
		freeArray(pointer);
		pointer = nullptr;
	};

	releaseVolume(m_dWorkingVolume);
	releaseVolume(m_dBaseVolume);
	releaseVolume(m_dBrushVolume);
	releaseVolume(m_dMirrorBrushVolume);
}

bool SingleParticleWorkspace::initializePixelBuffer(int width, int height) {
	if (width <= 0 || height <= 0 || !m_renderer) return false;
	if (m_pbo && m_tex && m_pboWidth == width && m_pboHeight == height)
		return true;

	releasePixelBuffer();

	glGenBuffers(1, &m_pbo);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pbo);
	glBufferData(
		GL_PIXEL_UNPACK_BUFFER,
		static_cast<std::size_t>(width) *
		static_cast<std::size_t>(height) * sizeof(GLubyte) * 4u,
		nullptr,
		GL_STREAM_DRAW);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

	glGenTextures(1, &m_tex);
	glBindTexture(GL_TEXTURE_2D, m_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glBindTexture(GL_TEXTURE_2D, 0);

	registerGLBufferObject(m_pbo, &m_cudaPboResource);
	m_cudaPboResourceSlot = &m_cudaPboResource;
	m_renderer->attachPixelBuffer(m_pbo);
	m_renderer->attachTexture(m_tex);
	m_pboWidth = width;
	m_pboHeight = height;
	return m_cudaPboResource != nullptr;
}

void SingleParticleWorkspace::releasePixelBuffer() {
	if (m_cudaPboResource) {
		unregisterGLBufferObject(m_cudaPboResource);
		m_cudaPboResource = nullptr;
	}
	if (m_pbo) {
		glDeleteBuffers(1, &m_pbo);
		m_pbo = 0;
	}
	if (m_tex) {
		glDeleteTextures(1, &m_tex);
		m_tex = 0;
	}
	if (m_renderer) {
		m_renderer->attachPixelBuffer(0);
		m_renderer->attachTexture(0);
	}
	m_pboWidth = 0;
	m_pboHeight = 0;
}

bool SingleParticleWorkspace::extractMarchingCubesMesh() {
	if (!m_marchingCubes || !m_dWorkingVolume) return false;
	if (m_volumeDirty) regenerateSPVolumeField(*this);
	m_marchingCubes->extract(m_dWorkingVolume, 0.0f);
	return m_marchingCubes->hasTriangleData();
}

WorkspacePresentation SingleParticleWorkspace::buildPresentation() const {
	if (!m_services || !m_services->arbiter) return WorkspacePresentation{};
	if (m_services->arbiter->isDomainSelection()) return buildLayer1Presentation();
	if (m_services->arbiter->isWorkspaceConfiguration()) return buildLayer2Presentation();
	return buildLayer3Presentation();
}

WorkspaceMenuPresentation SingleParticleWorkspace::buildMenu() const {
	WorkspaceMenuPresentation menu;
	const auto add = [&](const std::string& label, int command, bool enabled = true) {
		menu.items.push_back({ label, command, enabled });
	};
	const auto divider = [&]() { add("=========================================", 0, false); };

	if (!m_services || !m_services->arbiter ||
		!m_services->arbiter->isActiveWorkspace()) return menu;
	if (m_subLayer == SubLayer::Reference) {
		add(m_selectedParticle ? "* Deselect Particle"
			: m_selectionArmed ? "* Cancel Particle Selection"
			: "* Select Workplane Particle", MenuPrimary);
		divider();
		add("Collision Proxy:", 0, false);
		static const char* collision[] = {
			"SPHERE", "BLOCK", "CAPSULE", "CONE", "DEFORMABLE SPHERE"
		};
		for (int i = 0; i < 5; ++i)
			add(std::string(static_cast<int>(m_collisionShape) == i ? "* " : "  ") +
				collision[i], MenuCollisionBase + i);
		divider();
		add("* Load Static Particle", MenuLoad);
		add("* Save Active Particle", MenuSaveAs);
		add("* Rendering Setup", MenuToShape, m_selectedParticle);
		add("* Return to Layer 2", MenuBack);
	}
	else if (m_subLayer == SubLayer::ShapeEdit) {
		add("Render Source:", 0, false);
		add("* PARTICLE", MenuRenderSourceBase);
		add("* MESH", MenuRenderSourceBase + 1);
		add("Mesh Bound:", 0, false);
		add("* DEFAULT", MenuMeshBoundBase);
		add("* FILL", MenuMeshBoundBase + 1);
		add("Display Mode:", 0, false);
		add("* RENDER", MenuDisplayBase);
		add("* RENDER + COLLISION", MenuDisplayBase + 1);
		add("* WIREFRAME", MenuDisplayBase + 2);
		add("* Render Cage ON", MenuCageBase + 1);
		add("* Render Cage OFF", MenuCageBase);
		divider();
		add("* Mesh / Volume Preview and Edit", MenuToVolume);
		add("* Collision Setup", MenuToReference);
	}
	else if (m_subLayer == SubLayer::MarchingCubes) {
		add("* Save Static Particle", MenuSave);
		add("* Save Static Particle As", MenuSaveAs);
		add("* Export .OBJ", MenuExportObj);
		divider();
		add("* To Sub-Layer_2 Preview", MenuToVolume);
		add("* Return to Sub-Layer_0", MenuToReference);
	}
	else {
		add("Volume Assembly:", 0, false);
		add("* Node_0 Preview", MenuToPreview);
		add("* Node_1 Edit Object", MenuToEdit);
		add("* Node_2 Offset Object", MenuToOffset);
		add("* Node_3 Apply To Base", MenuToApply, canApplyVolumeToBase());
		add("* Run MC_Mode", MenuRunMarchingCubes,
			m_assemblyNode == AssemblyNode::Preview);
		if (m_assemblyNode == AssemblyNode::EditObject) {
			divider();
			add("Object Primitives:", 0, false);
			static const char* primitives[] = {
				"BASE", "SPHERE", "TORUS", "BLOCK", "CYLINDER",
				"CONE", "CAPSULE", "WEDGE", "DELTA_WING", "FRUSTUM"
			};
			for (int i = 0; i < 10; ++i)
				add(std::string("* ") + primitives[i], MenuPrimitiveBase + i);
			add("Scale Mode:", 0, false);
			add("* Whole", MenuScaleModeBase);
			add("* Z", MenuScaleModeBase + 1);
			add("* Y", MenuScaleModeBase + 2);
			add("* X", MenuScaleModeBase + 3);
			add("* Reset Scale", MenuResetScale);
			add("Rotation Mode:", 0, false);
			add("* Pitch", MenuRotationModeBase);
			add("* Yaw", MenuRotationModeBase + 1);
			add("* Roll", MenuRotationModeBase + 2);
			add("* Reset Rotation", MenuResetRotation);
			if (isEditingInjectionVoxel1()) {
				add("* Commit Brush Base", MenuCommitBrushBase);
				add("* Mirror NONE", MenuMirrorBase);
				add("* Mirror ON", MenuMirrorBase + 1);
			}
		}
		else if (m_assemblyNode == AssemblyNode::OffsetObject) {
			divider();
			add("Offset Vector:", 0, false);
			add("* X", MenuOffsetAxisBase);
			add("* Y", MenuOffsetAxisBase + 1);
			add("* Z", MenuOffsetAxisBase + 2);
			add("* Reset Object Offset", MenuResetOffset);
			if (hasInjectionVoxelSelected())
				add("* Toggle FUSE / CUT", MenuToggleInjectionMode);
		}
		else if (m_assemblyNode == AssemblyNode::ApplyToBase) {
			divider();
			add("* Commit New Base", MenuCommit);
		}
	}
	divider();
	add("* Back (Q)", MenuBack);
	return menu;
}

bool SingleParticleWorkspace::handleMenuCommand(
	int command,
	WorkspaceServices& services) {
	if (command == MenuPrimary)
		return handleInput({ WorkspaceInputAction::Activate }, services);
	if (command == MenuTogglePanel)
		return handleInput({ WorkspaceInputAction::Toggle }, services);
	if (command == MenuBack)
		return handleInput({ WorkspaceInputAction::Back }, services);
	if (command == MenuLoad) m_pendingHostRequest = HostRequest::LoadStaticParticle;
	else if (command == MenuSave) m_pendingHostRequest = HostRequest::SaveStaticParticle;
	else if (command == MenuSaveAs) m_pendingHostRequest = HostRequest::SaveStaticParticleAs;
	else if (command == MenuExportObj) m_pendingHostRequest = HostRequest::ExportObj;
	else if (command >= MenuCollisionBase && command < MenuCollisionBase + 5)
		m_collisionShape = static_cast<CollisionShape>(command - MenuCollisionBase);
	else if (command >= MenuRenderSourceBase && command < MenuRenderSourceBase + 2)
		m_particleRenderMode = static_cast<ParticleRenderMode>(command - MenuRenderSourceBase);
	else if (command >= MenuMeshBoundBase && command < MenuMeshBoundBase + 2)
		m_meshBoundMode = static_cast<MeshBoundMode>(command - MenuMeshBoundBase);
	else if (command >= MenuDisplayBase && command < MenuDisplayBase + 3)
		m_displayMode = static_cast<DisplayMode>(command - MenuDisplayBase);
	else if (command >= MenuCageBase && command < MenuCageBase + 2)
		m_renderCageVisible = command != MenuCageBase;
	else if (command == MenuToReference) {
		m_subLayer = SubLayer::Reference;
		m_assemblyNode = AssemblyNode::Preview;
		m_subLayerPanelOpen = false;
		m_activePanelItem = 0;
	}
	else if (command == MenuToShape && m_selectedParticle) {
		m_subLayer = SubLayer::ShapeEdit;
		m_subLayerPanelOpen = true;
		m_activePanelItem = 0;
	}
	else if (command == MenuToVolume) {
		m_subLayer = SubLayer::VolumeRender;
		m_assemblyNode = AssemblyNode::Preview;
		m_subLayerPanelOpen = true;
		m_activePanelItem = 0;
		markVolumeDirty();
	}
	else if (command == MenuRunMarchingCubes) enterMarchingCubes(services);
	else if (command == MenuToPreview) {
		m_assemblyNode = AssemblyNode::Preview; m_activePanelItem = 0; markVolumeDirty();
	}
	else if (command == MenuToEdit) {
		m_assemblyNode = AssemblyNode::EditObject; m_activePanelItem = 0; markVolumeDirty();
	}
	else if (command == MenuToOffset) {
		m_assemblyNode = AssemblyNode::OffsetObject; m_activePanelItem = 0;
	}
	else if (command == MenuToApply && canApplyVolumeToBase()) {
		m_assemblyNode = AssemblyNode::ApplyToBase; m_activePanelItem = 0;
	}
	else if (command == MenuCommit) {
		m_activePanelItem = 0; activateSubLayerPanelItem(services);
	}
	else if (command >= MenuPrimitiveBase && command < MenuPrimitiveBase + 10) {
		activeVolumeState().primitive =
			static_cast<VolumePrimitive>(command - MenuPrimitiveBase);
		markVolumeDirty();
	}
	else if (command >= MenuScaleModeBase && command < MenuScaleModeBase + 4) {
		m_objectEditMode = static_cast<ObjectEditMode>(command - MenuScaleModeBase);
		m_objectTransformMode = ObjectTransformMode::Scale;
	}
	else if (command >= MenuRotationModeBase && command < MenuRotationModeBase + 3) {
		m_objectRotationMode =
			static_cast<ObjectRotationMode>(command - MenuRotationModeBase);
		m_objectTransformMode = ObjectTransformMode::Rotation;
	}
	else if (command == MenuResetScale) resetObjectScale();
	else if (command == MenuResetRotation) resetObjectRotation();
	else if (command >= MenuOffsetAxisBase && command < MenuOffsetAxisBase + 3)
		m_offsetVector = static_cast<OffsetVector>(command - MenuOffsetAxisBase);
	else if (command == MenuResetOffset) resetObjectOffset();
	else if (command == MenuToggleInjectionMode) cycleVolumeInjectionMode(1);
	else if (command == MenuCommitBrushBase) commitBrushBase();
	else if (command >= MenuMirrorBase && command < MenuMirrorBase + 2) {
		m_mirrorMode = command == MenuMirrorBase ? MirrorMode::None : MirrorMode::On;
		markVolumeDirty();
	}
	else return false;
	updateCameraIntent(services);
	syncParticleRendering(services);
	return true;
}

SingleParticleWorkspace::OverlapPreviewStatus 
SingleParticleWorkspace::runtimeOverlapStatus() const {

	if (isSPOverlapPreviewActive())
		return OverlapPreviewStatus::Active;

	const bool outsideCage =
		(m_spOverlapPreviewSensorReady && m_spOverlapPreviewUnsafeCount > 0) ||
		(m_spOverlapPreviewMirrorRequired && m_spMirrorOverlapPreviewSensorReady &&
			m_spMirrorOverlapPreviewUnsafeCount > 0);

	if (outsideCage) return OverlapPreviewStatus::OutsideCage;
	return OverlapPreviewStatus::PositionInNode2;
}

WorkspacePresentation SingleParticleWorkspace::buildLayer1Presentation() const {
	WorkspacePresentation p;
	p.panelVisible = true;
	p.workspaceName = "LAYER 1 -> GRID_3D WORKSPACE CONFIGURATION";
	p.layerLabel = std::string("MODE: ") + workspaceName();
	WorkspacePanelSection section;
	section.rows.push_back(makeRow("[1]: GRID_3D SELECTION", workspaceName(),
		m_layer1Item == Layer1Item::Workspace));
	section.rows.push_back(makeRow("[2]: PARTICLE TYPE", objectTypeName(),
		m_layer1Item == Layer1Item::ParticleType));
	section.rows.push_back(makeRow("[3]: PRESS E TO CONFIGURE SIM", "",
		m_layer1Item == Layer1Item::Configure));
	p.sections.push_back(section);

	if (m_grid3DWorkspace != Grid3DWorkspace::SingleParticle)
		p.statusLine = "Selected GRID_3D workspace is reserved in this cartridge sprint.";
	else if (m_objectType == ObjectType::Static) {
		p.statusLine = "STATIC PARTICLE PIPELINE READY.";
		p.statusTone = WorkspaceStatusTone::Ready;
	}
	else {
		p.statusLine = m_objectType == ObjectType::Composite
			? "COMPOSITE PARTICLE PIPELINE RESERVED - VER 0.0.5."
			: "ATOMIC PARTICLE PIPELINE RESERVED.";
		p.statusTone = WorkspaceStatusTone::Warning;
	}
	p.footerLine1 = "W / S: Select list     A / D: Change value";
	p.footerLine2 = "E: Configure when LIST 3 selected     Q: Back one layer";
	return p;
}

WorkspacePresentation SingleParticleWorkspace::buildLayer2Presentation() const {
	WorkspacePresentation p;
	p.panelVisible = true;
	p.workspaceName = "LAYER 2 -> 3D_GRID MODE CONFIGURATION";
	p.layerLabel = "MODE: SINGLE_PARTICLE";
	WorkspacePanelSection section;
	section.rows.push_back(makeRow("[1]: PARTICLE COLOR SELECTION", particleColorName(),
		m_layer2Item == Layer2Item::Color));
	char radius[32];
	std::snprintf(radius, sizeof(radius), "%.4f", m_particleRadius);
	section.rows.push_back(makeRow("[2]: ADJUST PARTICLE RADIUS", radius,
		m_layer2Item == Layer2Item::Radius));
	section.rows.push_back(makeRow("[3]: PARTICLE RENDER MODE", particleRenderModeName(),
		m_layer2Item == Layer2Item::RenderMode));
	section.rows.push_back(makeRow("[4]: RUN SIMULATION LAYER", "",
		m_layer2Item == Layer2Item::Run));
	p.sections.push_back(section);
	p.footerLine1 = "W / S: Select list     A / D: Change value";
	p.footerLine2 = "E: Run when LIST 4 selected     Q: Back one layer";
	return p;
}

WorkspacePresentation SingleParticleWorkspace::buildLayer3Presentation() const {
	WorkspacePresentation p;
	p.runtimeStatus = buildRuntimeStatus();
	p.panelVisible = m_subLayerPanelOpen;
	p.workspaceName = "SINGLE_PARTICLE MODE";
	p.layerLabel = "LAYER 3 -> SIMULATION RUN (SINGLE_PARTICLE)";
	p.panelLayout = WorkspacePanelLayout::SubLayer;
	p.subLayerLabel = subLayerName();

	if (m_subLayer == SubLayer::Reference) {
		// ---------------------------------------------------------
		// Always construct the semantic Sub-Layer-0 presentation.
		//
		// panelVisible controls whether ViewPort is opening or
		// closing the panel.  The presentation data must survive
		// during the slide-out animation.
		// ---------------------------------------------------------
		appendReferencePanel(p);

		if (m_selectionArmed) 
			p.statusLine = "PARTICLE SELECTION ARMED";
		else
			p.statusLine = "E: Arm particle selection";

		p.footerLine1 = m_subLayerPanelOpen
			? "W/S: Select    A/D: Change value    E: Activate    TAB: Hide"
			: m_selectedParticle
			? "E: Deselect particle    TAB: Collision setup panel"
			: "W/S: Move workplane    LMB: Select particle";
		p.footerLine2 = "Q: Back";
	}
	else if (m_subLayer == SubLayer::ShapeEdit) {
		appendShapePanel(p);
		p.statusLine = m_selectedParticle
			? "PARTICLE SELECTED - RENDERING SETUP ACTIVE"
			: "SELECT A PARTICLE IN SUB-LAYER_0";
		p.footerLine1 = m_subLayerPanelOpen
			? "W/S: Select    A/D: Change value    E: Activate    TAB: Hide"
			: "TAB: Show rendering setup panel";
		p.footerLine2 = "Q: Back";
	}
	else if (m_subLayer == SubLayer::MarchingCubes) {
		if (m_subLayerPanelOpen) appendVolumePanel(p);
		p.statusLine = m_marchingCubes && m_marchingCubes->hasTriangleData()
			? "STATUS: TRIANGLE MESH READY"
			: "STATUS: NO ISO-SURFACE DETECTED";
		p.statusTone = m_marchingCubes && m_marchingCubes->hasTriangleData()
			? WorkspaceStatusTone::Ready : WorkspaceStatusTone::Warning;
		p.footerLine1 = "W/S: Select    E: Activate    TAB: Hide";
		p.footerLine2 = "Q: Back";
	}
	else {
		if (m_subLayerPanelOpen) appendVolumePanel(p);
		static const char* nodeNames[] = {
			"Node_0 -> Preview Object", "Node_1 -> Edit Object",
			"Node_2 -> Offset Object", "Node_3 -> Apply To Base"
		};
		p.statusLine = nodeNames[static_cast<int>(m_assemblyNode)];
		p.footerLine1 = m_subLayerPanelOpen
			? "W/S: Select    A/D: Change value    E: Activate    TAB: Hide"
			: "W/S: Scale / Offset    A/D: Rotate    TAB: Show panel";
		p.footerLine2 = "E: Advance    Q: Back    RMB: Menu";
	}
	return p;
}

WorkspacePresentation SingleParticleWorkspace::buildLayer1TransitionPresentation() const {
	return buildLayer1Presentation();
}

WorkspaceRuntimeStatus SingleParticleWorkspace::buildRuntimeStatus() const {

	WorkspaceRuntimeStatus status;
	status.visible = true;

	status.titleLine =
		"LAYER 3 -> SIMULATION RUN "
		"(SINGLE_PARTICLE)";

	status.contextLine =
		string("SINGLE_PARTICLE: ") +
		runtimeSubLayerName();

	status.objectLine =
		buildRuntimeObjectLine();

	status.helpLine =
		buildRuntimeHelpLine();

	// ---------------------------------------------------------
	// GOLD expands the status HUD during shared-volume
	// overlap preview in Node_1 / Node_2.
	// ---------------------------------------------------------
	status.auxiliaryVisible = m_subLayer == 
		SubLayer::VolumeRender &&
		hasInjectionVoxelSelected() &&
		(m_assemblyNode == AssemblyNode::EditObject ||
			m_assemblyNode == AssemblyNode::OffsetObject);

	if (status.auxiliaryVisible) {

		const OverlapPreviewStatus overlap =
			runtimeOverlapStatus();

		status.auxiliaryStatusLine =
			string("OVERLAP PREVIEW { ") +
			overlapStatusName(overlap) +
			" }";

		switch (overlap) {

		case OverlapPreviewStatus::Active:
			status.auxiliaryStatusTone = WorkspaceStatusTone::Ready;
			break;

		case OverlapPreviewStatus::OutsideCage:
			status.auxiliaryStatusTone = WorkspaceStatusTone::Caution;
			break;

		default:
			status.auxiliaryStatusTone = WorkspaceStatusTone::Neutral;
			break;
		}

		status.auxiliaryReferenceLine =
			"REFERENCE FRAME { VOLUME_0 }";

		status.auxiliaryTargetLine =
			string("EDIT TARGET { ") +
			editTargetName() +
			" }";
	}

	return status;
}

void SingleParticleWorkspace::appendReferencePanel(WorkspacePresentation& p) const {
	WorkspacePanelSection object;
	object.heading = "Particle Object:";
	object.rows.push_back(makeRow("OBJECT TYPE", objectTypeName(), false));
	p.sections.push_back(object);

	WorkspacePanelSection collision;
	collision.heading = "Collision Proxy:";
	collision.rows.push_back(makeRow("[1]: COLLISION SHAPE", collisionShapeName(),
		m_activePanelItem == 0));
	p.sections.push_back(collision);

	WorkspacePanelSection assets;
	assets.heading = "Static Particle Asset:";
	assets.rows.push_back(makeRow("[2]: LOAD STATIC PARTICLE", "", m_activePanelItem == 1));
	assets.rows.push_back(makeRow("[3]: SAVE ACTIVE PARTICLE", "", m_activePanelItem == 2));
	p.sections.push_back(assets);

	WorkspacePanelSection next;
	next.heading = "Next Sub-Layer:";
	next.rows.push_back(makeRow("[4]: RENDERING SETUP", "", m_activePanelItem == 3));
	p.sections.push_back(next);
}

void SingleParticleWorkspace::appendShapePanel(WorkspacePresentation& p) const {
	WorkspacePanelSection render;
	render.heading = "Render Source:";
	render.rows.push_back(makeRow("[1]: RENDER SOURCE",
		m_particleRenderMode == ParticleRenderMode::Mesh ? "MESH" : "PARTICLE",
		m_activePanelItem == 0));
	p.sections.push_back(render);

	WorkspacePanelSection bound;
	bound.heading = "Mesh Scale Bound:";
	bound.rows.push_back(makeRow("[2]: MESH BOUND",
		m_meshBoundMode == MeshBoundMode::Fill ? "FILL" : "DEFAULT",
		m_activePanelItem == 1));
	p.sections.push_back(bound);

	WorkspacePanelSection debug;
	debug.heading = "Debug Presentation:";
	const char* display = m_displayMode == DisplayMode::Wireframe
		? "WIREFRAME" : m_displayMode == DisplayMode::RenderAndCollision
		? "RENDER + COLLISION" : "RENDER";
	debug.rows.push_back(makeRow("[3]: DISPLAY MODE", display,
		m_activePanelItem == 2));
	debug.rows.push_back(makeRow("[4]: RENDER CAGE",
		m_renderCageVisible ? "ON" : "OFF", m_activePanelItem == 3));
	p.sections.push_back(debug);

	WorkspacePanelSection traversal;
	traversal.heading = "Next / Previous Sub-Layer:";
	traversal.rows.push_back(makeRow("[5]: MESH / VOLUME PREVIEW AND EDIT", "",
		m_activePanelItem == 4));
	traversal.rows.push_back(makeRow("[6]: RETURN TO COLLISION SETUP", "",
		m_activePanelItem == 5));
	p.sections.push_back(traversal);
}

void SingleParticleWorkspace::appendVolumePanel(WorkspacePresentation& p) const {
	if (m_subLayer == SubLayer::MarchingCubes) {
		WorkspacePanelSection report;
		report.heading = "MC CLASSIFICATION REPORT";
		if (m_marchingCubes) {
			const uint3 grid = m_marchingCubes->getGridSize();
			char value[160];
			std::snprintf(value, sizeof(value), "%u x %u x %u    |    ISO: 0.0000",
				grid.x, grid.y, grid.z);
			report.rows.push_back(makeRow("Grid:", value, false));
			std::snprintf(value, sizeof(value), "%u / %u",
				m_marchingCubes->getActiveVoxelCount(),
				m_marchingCubes->getNumVoxels());
			report.rows.push_back(makeRow("Active cells:", value, false));
			std::snprintf(value, sizeof(value), "%u vertices    |    %u triangles",
				m_marchingCubes->getTotalVertexCount(),
				m_marchingCubes->getGeneratedTriangleCount());
			report.rows.push_back(makeRow("Output:", value, false));
		}
		p.sections.push_back(report);
		WorkspacePanelSection output;
		output.heading = "Output:";
		output.rows.push_back(makeRow("[1]: SAVE STATIC PARTICLE", "",
			m_activePanelItem == 0));
		output.rows.push_back(makeRow("[2]: SAVE STATIC PARTICLE AS", "",
			m_activePanelItem == 1));
		output.rows.push_back(makeRow("[3]: EXPORT .OBJ", "",
			m_activePanelItem == 2));
		p.sections.push_back(output);
		WorkspacePanelSection back;
		back.heading = "Previous / Next Sub-layer:";
		back.rows.push_back(makeRow("[4]: To Sub-Layer_2 Preview", "",
			m_activePanelItem == 3));
		back.rows.push_back(makeRow("[5]: RETURN TO SUB-LAYER_0", "",
			m_activePanelItem == 4));
		p.sections.push_back(back);
		return;
	}

	if (m_assemblyNode == AssemblyNode::Preview) {
		WorkspacePanelSection injection;
		injection.heading = "Volume Injection:";
		injection.rows.push_back(makeRow("[1]: Injection Voxels",
			injectionVoxelName(), m_activePanelItem == 0));
		p.sections.push_back(injection);
		WorkspacePanelSection next;
		next.heading = "Next Node / Sub-Layer:";
		next.rows.push_back(makeRow("[2]: Edit Object", "", m_activePanelItem == 1));
		next.rows.push_back(makeRow("[3]: Run MC_Mode", "", m_activePanelItem == 2));
		p.sections.push_back(next);
		return;
	}

	static constexpr int rotationSteps[] = { 1, 20, 45, 90, 180 };
	const char* target = isEditingInjectionVoxel1() ? "VOLUME_1" : "VOLUME_0";
	if (m_assemblyNode == AssemblyNode::EditObject) {
		WorkspacePanelSection edit;
		edit.heading = hasInjectionVoxelSelected()
			? "Select Volume To Edit:" : "=== Edit Volume_0 ===";
		if (hasInjectionVoxelSelected())
			edit.rows.push_back(makeRow("[1]: Edit", target, m_activePanelItem == 0));
		edit.rows.push_back(makeRow(hasInjectionVoxelSelected()
			? "[2]: Select Object" : "[1]: Select Object",
			volumePrimitiveName(),
			m_activePanelItem == (hasInjectionVoxelSelected() ? 1 : 0)));
		if (!hasInjectionVoxelSelected() || isEditingInjectionVoxel0()) {
			char degrees[24];
			std::snprintf(degrees, sizeof(degrees), "%d",
				rotationSteps[m_rotationIncrementIndex]);
			edit.rows.push_back(makeRow(hasInjectionVoxelSelected()
				? "[3]: Deg" : "[2]: Deg", degrees,
				m_activePanelItem == (hasInjectionVoxelSelected() ? 2 : 1)));
			edit.rows.push_back(makeRow(hasInjectionVoxelSelected()
				? "[4]: Offset Object" : "[3]: Offset Object", "",
				m_activePanelItem == (hasInjectionVoxelSelected() ? 3 : 2)));
			edit.rows.push_back(makeRow(hasInjectionVoxelSelected()
				? "[5]: Preview Object" : "[4]: Preview Object", "",
				m_activePanelItem == (hasInjectionVoxelSelected() ? 4 : 3)));
		}
		else {
			edit.rows.push_back(makeRow("[3]: Commit Brush Base", "",
				m_activePanelItem == 2));
			edit.rows.push_back(makeRow("[4]: MIRROR",
				m_mirrorMode == MirrorMode::On ? "ON" : "NONE",
				m_activePanelItem == 3));
		}
		p.sections.push_back(edit);
		return;
	}

	if (m_assemblyNode == AssemblyNode::OffsetObject) {
		WorkspacePanelSection offset;
		offset.heading = hasInjectionVoxelSelected()
			? "Edit Offset Volume:" : "Offset Grid Vector:";
		int row = 0;
		if (hasInjectionVoxelSelected())
			offset.rows.push_back(makeRow("[1]: Select", target,
				m_activePanelItem == row++));
		const char* axis = m_offsetVector == OffsetVector::Y ? "Y"
			: m_offsetVector == OffsetVector::Z ? "Z" : "X";
		offset.rows.push_back(makeRow(hasInjectionVoxelSelected()
			? "[2]: Offset" : "[1]: Offset", axis, m_activePanelItem == row++));
		char increment[32];
		std::snprintf(increment, sizeof(increment), "%.3g", m_offsetIncrement);
		offset.rows.push_back(makeRow(hasInjectionVoxelSelected()
			? "[3]: Increments" : "[2]: Increments", increment,
			m_activePanelItem == row++));
		if (hasInjectionVoxelSelected() && isEditingInjectionVoxel1()) {
			offset.rows.push_back(makeRow("[4]: Inject",
				m_injectionMode == InjectionMode::Cut ? "CUT" : "FUSE",
				m_activePanelItem == 3));
		}
		else {
			if (hasInjectionVoxelSelected()) {
				char rail[32];
				std::snprintf(rail, sizeof(rail), "%.2f", m_injectionRailT);
				offset.rows.push_back(makeRow("[4]: Injection Vector", rail,
					m_activePanelItem == 3));
			}
			const bool ready = canApplyVolumeToBase();
			std::string state = !m_volumeBoundarySensorReady ? "CHECKING"
				: ready ? "READY" : "BLOCKED: " +
					std::to_string(m_volumeBoundaryUnsafeCount) + " PATCHES";
			WorkspacePanelRow apply = makeRow(hasInjectionVoxelSelected()
				? (m_injectionMode == InjectionMode::Cut
					? "[5]: Cut Voxel From Anchor" : "[5]: Fuse Voxel To Anchor")
				: "[3]: Apply To Base", state,
				m_activePanelItem == (hasInjectionVoxelSelected() ? 4 : 2));
			apply.selectable = ready;
			offset.rows.push_back(apply);
			offset.rows.push_back(makeRow(hasInjectionVoxelSelected()
				? "[6]: Edit Object" : "[4]: Edit Object", "",
				m_activePanelItem == (hasInjectionVoxelSelected() ? 5 : 3)));
		}
		p.sections.push_back(offset);
		return;
	}

	WorkspacePanelSection apply;
	apply.heading = std::string("Apply Operation Mode ") +
		(m_injectionMode == InjectionMode::Cut ? "{ CUT }" : "{ FUSE }");
	apply.rows.push_back(makeRow("[1]: Commit / Loop Back To Preview", "",
		m_activePanelItem == 0));
	apply.rows.push_back(makeRow("[2]: Preview Object", "",
		m_activePanelItem == 2));
	apply.rows.push_back(makeRow("[3]: Offset Object", "",
		m_activePanelItem == 1));
	p.sections.push_back(apply);
}

const char* SingleParticleWorkspace::workspaceName() const {
	switch (m_grid3DWorkspace) {
	case Grid3DWorkspace::Graph3D: return "GRAPH_3D";
	case Grid3DWorkspace::LinkedParticles: return "LINKED_PARTICLES";
	default: return "SINGLE_PARTICLE";
	}
}

const char* SingleParticleWorkspace::objectTypeName() const {
	switch (m_objectType) {
	case ObjectType::Composite: return "COMPOSITE";
	case ObjectType::Atomic: return "ATOMIC";
	default: return "STATIC";
	}
}

const char* SingleParticleWorkspace::particleColorName() const {
	switch (m_particleColor) {
	case ParticleColor::Blue: return "BLUE";
	case ParticleColor::Green: return "GREEN";
	default: return "RED";
	}
}

const char* SingleParticleWorkspace::particleRenderModeName() const {
	return m_particleRenderMode == ParticleRenderMode::Mesh ? "MESH" : "DEFAULT";
}

const char* SingleParticleWorkspace::subLayerName() const {
	switch (m_subLayer) {
	case SubLayer::ShapeEdit: return "SUB-LAYER_1 -> RENDERING SETUP";
	case SubLayer::VolumeRender: return "SUB-LAYER_2 -> MESH / VOLUME PREVIEW AND EDIT";
	case SubLayer::MarchingCubes: return "SUB-LAYER_3 -> MARCHING CUBES";
	default: return "SUB-LAYER_0 -> COLLISION SETUP";
	}
}

const char* SingleParticleWorkspace::collisionShapeName() const {
	switch (m_collisionShape) {
	case CollisionShape::Block: return "BLOCK";
	case CollisionShape::Capsule: return "CAPSULE";
	case CollisionShape::Cone: return "CONE";
	case CollisionShape::DeformableSphere: return "DEFORMABLE SPHERE";
	default: return "SPHERE";
	}
}

const char* SingleParticleWorkspace::volumePrimitiveName() const {
	switch (getResolvedVolumePrimitiveSelection()) {
	case VolumePrimitive::Base: return "BASE";
	case VolumePrimitive::Torus: return "TORUS";
	case VolumePrimitive::Block: return "BLOCK";
	case VolumePrimitive::Cylinder: return "CYLINDER";
	case VolumePrimitive::Cone: return "CONE";
	case VolumePrimitive::Capsule: return "CAPSULE";
	case VolumePrimitive::Wedge: return "WEDGE";
	case VolumePrimitive::DeltaWing: return "DELTA_WING";
	case VolumePrimitive::Frustum: return "FRUSTUM";
	default: return "SPHERE";
	}
}

const char* SingleParticleWorkspace::injectionVoxelName() const {
	static const char* names[] = {
		"NONE", "VOXEL_211", "VOXEL_121", "VOXEL_011", "VOXEL_112",
		"VOXEL_101", "VOXEL_110", "VOXEL_120", "VOXEL_221", "VOXEL_122",
		"VOXEL_021", "VOXEL_201", "VOXEL_100", "VOXEL_001", "VOXEL_102",
		"VOXEL_010", "VOXEL_210", "VOXEL_012", "VOXEL_212", "VOXEL_202",
		"VOXEL_020", "VOXEL_220", "VOXEL_002", "VOXEL_200", "VOXEL_022",
		"VOXEL_222", "VOXEL_000"
	};
	const int value = static_cast<int>(m_injectionVoxel);
	return value >= 0 && value < static_cast<int>(InjectionVoxel::Count)
		? names[value] : "NONE";
}

const char* SingleParticleWorkspace::runtimeSubLayerName() const {
	switch (m_subLayer) {

	case SubLayer::Reference:
		return "SUB_LAYER_0 COLLISION SETUP";

	case SubLayer::ShapeEdit:
		return "SUB_LAYER_1 RENDERING SETUP";

	case SubLayer::VolumeRender:
		return "SUB_LAYER_2 MESH / VOLUME PREVIEW AND EDIT";

	case SubLayer::MarchingCubes:
		return "SUB_LAYER_3 MARCHING CUBES";

	default:
		return "UNKNOWN_SINGLE_PARTICLE_SUB_LAYER";
	}
}

const char* SingleParticleWorkspace::objectTransformModeName() const {
	return m_objectTransformMode == ObjectTransformMode::Rotation
		? "Rotation"
		: "Scale";
}

const char* SingleParticleWorkspace::objectEditModeName() const {
	switch (m_objectEditMode) {

	case ObjectEditMode::ScaleZ:
		return "Scale z-axis";

	case ObjectEditMode::ScaleY:
		return "Scale y-axis";

	case ObjectEditMode::ScaleX:
		return "Scale x-axis";

	default:
		return "Scale whole object";
	}
}

const char* SingleParticleWorkspace::objectRotationModeName() const {
	switch (m_objectRotationMode) {

	case ObjectRotationMode::Yaw:
		return "Yaw";

	case ObjectRotationMode::Roll:
		return "Roll";

	default:
		return "Pitch";
	}
}

const char* SingleParticleWorkspace::offsetVectorName() const {
	switch (m_offsetVector) {

	case OffsetVector::Y:
		return "Y-VECTOR";

	case OffsetVector::Z:
		return "Z-VECTOR";

	default:
		return "X-VECTOR";
	}
}

const char* SingleParticleWorkspace::editTargetName() const {
	return m_editTarget == EditTarget::Volume1
		? "VOLUME_1"
		: "VOLUME_0";
}

const char* SingleParticleWorkspace::overlapStatusName(OverlapPreviewStatus status) const {
	switch (status) {

	case OverlapPreviewStatus::Active:
		return "ACTIVE";

	case OverlapPreviewStatus::OutsideCage:
		return "OUTSIDE CAGE";

	default:
		return "POSITION IN NODE_2";
	}
}

string SingleParticleWorkspace::buildRuntimeObjectLine() const {
	if (m_subLayer != SubLayer::VolumeRender) {
		return "OBJECT: SINGLE_PARTICLE";
	}

	char line[512];

	const VolumeObjectState& state =
		getActiveVolumeState();

	// =========================================================
	// NODE 2 — OFFSET
	// =========================================================
	if (m_assemblyNode == AssemblyNode::OffsetObject) {

		const char* boundary =
			!m_volumeBoundarySensorReady
			? "CHECKING"
			: (m_volumeBoundaryUnsafeCount == 0 ? "SAFE": "CONTACT");

		snprintf(
			line,
			sizeof(line),

			"OBJECT: %s | "
			"OFFSET %.3f/%.3f/%.3f | "
			"VECTOR %s | "
			"INC %.3g | "
			"BOUNDARY %s:%u",

			volumePrimitiveName(),

			state.offsetX,
			state.offsetY,
			state.offsetZ,

			offsetVectorName(),

			m_offsetIncrement,

			boundary,
			m_volumeBoundaryUnsafeCount
		);

		return line;
	}

	// =========================================================
	// NODE 0 / NODE 1 / NODE 3
	// =========================================================
	snprintf(
		line,
		sizeof(line),

		"OBJECT: %s | "
		"MODE: %s | "
		"SCALE %s %.2f/%.2f/%.2f/%.2f | "
		"ROT %s %.0f/%.0f/%.0f | "
		"Deg %d",

		volumePrimitiveName(),
		objectTransformModeName(),

		objectEditModeName(),

		state.scaleWhole,
		state.scaleX,
		state.scaleY,
		state.scaleZ,

		objectRotationModeName(),

		state.pitchDeg,
		state.yawDeg,
		state.rollDeg,

		rotationIncrementDegrees()
	);

	return line;
}

string SingleParticleWorkspace::buildRuntimeHelpLine() const {
	if (m_subLayer == SubLayer::Reference) {

		if (m_subLayerPanelOpen) {

			return
				"TAB: Hide panel    "
				"W/S: Select item    "
				"A/D: Change value    "
				"E: Activate    "
				"Q: Back";
		}

		if (m_selectedParticle) {

			return
				"E: Deselect particle    "
				"TAB: Collision setup panel    "
				"Q: Back";
		}

		if (m_selectionArmed) {

			return
				"W/S: Move workplane    "
				"LMB: Select particle    "
				"E: Cancel selection mode    "
				"Q: Back";
		}

		return
			"E: Arm particle selection    "
			"Q: Back";
	}

	if (m_subLayer == SubLayer::ShapeEdit) {

		if (m_subLayerPanelOpen) {

			return
				"TAB: Hide panel    "
				"W/S: Select item    "
				"A/D: Change value    "
				"E: Activate    "
				"Q: Back";
		}

		return
			"TAB: Rendering setup panel    "
			"Q: Collision setup";
	}

	if (m_subLayer == SubLayer::VolumeRender) {

		return
			"TAB: Toggle sub-layer panel    "
			"E: Advance    "
			"Q: Back    "
			"RMB: Menu";
	}

	return
		"E: Advance sub-layer    "
		"Q: Back    "
		"RMB: Menu";
}

int SingleParticleWorkspace::rotationIncrementDegrees() const {

	static constexpr int steps[] = { 1, 20, 45, 90, 180 };

	const int count = static_cast<int>(sizeof(steps) /
		sizeof(steps[0]));

	const int index =
		(std::max)(0, std::min(count - 1, m_rotationIncrementIndex));

	return steps[index];
}

size_t SingleParticleWorkspace::getVolumeBytes() const {
	return static_cast<std::size_t>(m_volumeSize.x) *
		static_cast<std::size_t>(m_volumeSize.y) *
		static_cast<std::size_t>(m_volumeSize.z) * sizeof(float);
}

bool SingleParticleWorkspace::exportWorkingVolumeToHost(
	std::vector<float>& output) const {
	output.clear();
	if (!m_dWorkingVolume) return false;

	const std::size_t sampleCount =
		static_cast<std::size_t>(m_volumeSize.x) *
		static_cast<std::size_t>(m_volumeSize.y) *
		static_cast<std::size_t>(m_volumeSize.z);
	if (sampleCount == 0u) return false;

	output.resize(sampleCount);
	const cudaError_t copyStatus = cudaMemcpy(
		output.data(), m_dWorkingVolume, getVolumeBytes(), cudaMemcpyDeviceToHost);
	if (copyStatus != cudaSuccess) {
		std::printf("[SingleParticleWorkspace] Native volume export failed: %s\n",
			cudaGetErrorString(copyStatus));
		output.clear();
		return false;
	}
	threadSync();

	for (float sample : output) {
		if (!std::isfinite(sample)) {
			output.clear();
			return false;
		}
	}
	return true;
}

bool SingleParticleWorkspace::restoreCommittedVolumeFromHost(
	const std::vector<float>& input,
	const int3& sourceSize) {
	if (!m_dBaseVolume || !m_dWorkingVolume) return false;
	if (sourceSize.x != m_volumeSize.x ||
		sourceSize.y != m_volumeSize.y ||
		sourceSize.z != m_volumeSize.z) return false;

	const std::size_t expectedSamples =
		static_cast<std::size_t>(m_volumeSize.x) *
		static_cast<std::size_t>(m_volumeSize.y) *
		static_cast<std::size_t>(m_volumeSize.z);
	if (input.size() != expectedSamples) return false;
	for (float sample : input) if (!std::isfinite(sample)) return false;

	if (cudaMemcpy(m_dBaseVolume, input.data(), getVolumeBytes(),
		cudaMemcpyHostToDevice) != cudaSuccess) return false;
	if (cudaMemcpy(m_dWorkingVolume, input.data(), getVolumeBytes(),
		cudaMemcpyHostToDevice) != cudaSuccess) return false;

	if (m_dBrushVolume)
		clearVolumeKernelLauncher(m_dBrushVolume, m_volumeSize, 1.0e6f);
	if (m_dMirrorBrushVolume)
		clearVolumeKernelLauncher(m_dMirrorBrushVolume, m_volumeSize, 1.0e6f);
	threadSync();

	m_committedVolumeReady = true;
	m_hasCommittedGeometry = true;
	m_volumeDirty = false;
	m_volumeBoundarySensorReady = false;
	m_volumeBoundaryUnsafeCount = 0;
	clearSPOverlapPreviewStatus();
	return true;
}

SingleParticleWorkspace::VolumeObjectState&
SingleParticleWorkspace::activeVolumeState() {
	if (hasInjectionVoxelSelected() && isEditingInjectionVoxel1())
		return m_volume1State;
	return m_volume0State;
}

const SingleParticleWorkspace::VolumeObjectState&
SingleParticleWorkspace::activeVolumeState() const {
	if (hasInjectionVoxelSelected() && isEditingInjectionVoxel1())
		return m_volume1State;
	return m_volume0State;
}

const SingleParticleWorkspace::VolumeObjectState&
SingleParticleWorkspace::getActiveVolumeState() const {
	return activeVolumeState();
}

bool SingleParticleWorkspace::isInjectionBrushBaseSelected() const {
	return hasInjectionVoxelSelected() && isEditingInjectionVoxel1() &&
		getActiveVolumeState().primitive == VOLUME_PRIMITIVE_BASE;
}

SingleParticleWorkspace::VolumePrimitive
SingleParticleWorkspace::getResolvedVolumePrimitiveSelection() const {
	const VolumeObjectState& state = getActiveVolumeState();
	if (hasInjectionVoxelSelected() && isEditingInjectionVoxel1() &&
		state.primitive == VOLUME_PRIMITIVE_BASE) {
		if (!state.brushBaseReady ||
			state.brushBasePrimitive == VOLUME_PRIMITIVE_BASE)
			return VOLUME_PRIMITIVE_SPHERE;
		return state.brushBasePrimitive;
	}
	return state.primitive;
}

void SingleParticleWorkspace::resetVolumeState(
	VolumeObjectState& state,
	VolumePrimitive primitive) {
	state = VolumeObjectState{};
	state.primitive = primitive;
	state.brushBasePrimitive = primitive == VOLUME_PRIMITIVE_BASE
		? VOLUME_PRIMITIVE_SPHERE : primitive;
}

float SingleParticleWorkspace::getEffectiveVolumeScaleX() const {
	const VolumeObjectState& state = getActiveVolumeState();
	return state.scaleWhole * state.scaleX;
}

float SingleParticleWorkspace::getEffectiveVolumeScaleY() const {
	const VolumeObjectState& state = getActiveVolumeState();
	return state.scaleWhole * state.scaleY;
}

float SingleParticleWorkspace::getEffectiveVolumeScaleZ() const {
	const VolumeObjectState& state = getActiveVolumeState();
	return state.scaleWhole * state.scaleZ;
}

SingleParticleWorkspace::BasisVector
SingleParticleWorkspace::transformByBasis(
	const ObjectBasis& basis,
	const BasisVector& localVector) const {
	return {
		basis.xAxis.x * localVector.x + basis.yAxis.x * localVector.y +
			basis.zAxis.x * localVector.z,
		basis.xAxis.y * localVector.x + basis.yAxis.y * localVector.y +
			basis.zAxis.y * localVector.z,
		basis.xAxis.z * localVector.x + basis.yAxis.z * localVector.y +
			basis.zAxis.z * localVector.z
	};
}

SingleParticleWorkspace::BasisVector
SingleParticleWorkspace::rotateLocalVectorXYZ(
	const BasisVector& vector,
	float pitchDeg,
	float yawDeg,
	float rollDeg) const {
	constexpr float kDegToRad = 0.01745329251994329577f;
	const float pitch = pitchDeg * kDegToRad;
	const float yaw = yawDeg * kDegToRad;
	const float roll = rollDeg * kDegToRad;
	const float sp = std::sin(pitch), cp = std::cos(pitch);
	const float sy = std::sin(yaw), cy = std::cos(yaw);
	const float sr = std::sin(roll), cr = std::cos(roll);
	const BasisVector afterPitch{
		vector.x,
		cp * vector.y - sp * vector.z,
		sp * vector.y + cp * vector.z
	};
	const BasisVector afterYaw{
		cy * afterPitch.x + sy * afterPitch.z,
		afterPitch.y,
		-sy * afterPitch.x + cy * afterPitch.z
	};
	return {
		cr * afterYaw.x - sr * afterYaw.y,
		sr * afterYaw.x + cr * afterYaw.y,
		afterYaw.z
	};
}

SingleParticleWorkspace::ObjectBasis
SingleParticleWorkspace::orthonormalizeBasis(const ObjectBasis& basis) const {
	const auto dot = [](const BasisVector& a, const BasisVector& b) {
		return a.x * b.x + a.y * b.y + a.z * b.z;
	};
	const auto scale = [](const BasisVector& value, float amount) {
		return BasisVector{ value.x * amount, value.y * amount, value.z * amount };
	};
	const auto subtract = [](const BasisVector& a, const BasisVector& b) {
		return BasisVector{ a.x - b.x, a.y - b.y, a.z - b.z };
	};
	const auto cross = [](const BasisVector& a, const BasisVector& b) {
		return BasisVector{
			a.y * b.z - a.z * b.y,
			a.z * b.x - a.x * b.z,
			a.x * b.y - a.y * b.x
		};
	};
	const auto normalize = [&](const BasisVector& value) {
		const float lengthSquared = dot(value, value);
		if (lengthSquared <= 1.0e-12f) return BasisVector{};
		return scale(value, 1.0f / std::sqrt(lengthSquared));
	};

	BasisVector x = normalize(basis.xAxis);
	BasisVector y = normalize(subtract(basis.yAxis, scale(x, dot(basis.yAxis, x))));
	BasisVector z = normalize(cross(x, y));
	y = normalize(cross(z, x));
	return { x, y, z };
}

SingleParticleWorkspace::ObjectBasis
SingleParticleWorkspace::getEffectiveObjectBasis() const {
	const VolumeObjectState& state = activeVolumeState();
	const BasisVector localX = rotateLocalVectorXYZ(
		{ 1.0f, 0.0f, 0.0f }, state.pitchDeg, state.yawDeg, state.rollDeg);
	const BasisVector localY = rotateLocalVectorXYZ(
		{ 0.0f, 1.0f, 0.0f }, state.pitchDeg, state.yawDeg, state.rollDeg);
	const BasisVector localZ = rotateLocalVectorXYZ(
		{ 0.0f, 0.0f, 1.0f }, state.pitchDeg, state.yawDeg, state.rollDeg);
	return orthonormalizeBasis({
		transformByBasis(state.basis, localX),
		transformByBasis(state.basis, localY),
		transformByBasis(state.basis, localZ)
	});
}

int SingleParticleWorkspace::getInjectionVoxelDX() const {
	const int value = static_cast<int>(m_injectionVoxel);
	return value >= 0 && value < static_cast<int>(InjectionVoxel::Count)
		? kInjectionDirections[value].x : 0;
}

int SingleParticleWorkspace::getInjectionVoxelDY() const {
	const int value = static_cast<int>(m_injectionVoxel);
	return value >= 0 && value < static_cast<int>(InjectionVoxel::Count)
		? kInjectionDirections[value].y : 0;
}

int SingleParticleWorkspace::getInjectionVoxelDZ() const {
	const int value = static_cast<int>(m_injectionVoxel);
	return value >= 0 && value < static_cast<int>(InjectionVoxel::Count)
		? kInjectionDirections[value].z : 0;
}

void SingleParticleWorkspace::getMirroredInjectionDirection(
	int& dx, int& dy, int& dz) const {
	dx = -getInjectionVoxelDX();
	dy = -getInjectionVoxelDY();
	dz = -getInjectionVoxelDZ();
}

void SingleParticleWorkspace::cycleVolumePrimitiveSelection(int direction) {
	if (direction == 0) return;
	if (m_authoringSource == AuthoringSource::LoadedStaticMesh &&
		!m_loadedStaticMeshHasEditableVolume && isEditingInjectionVoxel0()) {
		m_volume0State.primitive = VOLUME_PRIMITIVE_BASE;
		return;
	}
	VolumeObjectState& state = activeVolumeState();
	state.primitive = cycleEnum(state.primitive,
		static_cast<int>(VolumePrimitive::Count), direction);
	markVolumeDirty();
}

void SingleParticleWorkspace::cycleInjectionVoxelSelection(int direction) {
	if (direction == 0) return;
	m_injectionVoxel = cycleEnum(m_injectionVoxel,
		static_cast<int>(InjectionVoxel::Count), direction);
	if (m_injectionVoxel == InjectionVoxel::None)
		m_editTarget = EditTarget::Volume0;
	m_injectionRailT = 0.0f;
	markVolumeDirty();
}

void SingleParticleWorkspace::cycleVolumeEditTarget(int direction) {
	m_editTarget = cycleEnum(m_editTarget,
		static_cast<int>(EditTarget::Count), direction);
	markVolumeDirty();
}

void SingleParticleWorkspace::cycleVolumeInjectionMode(int direction) {
	if (direction == 0) return;
	m_injectionMode = m_injectionMode == InjectionMode::Fuse
		? InjectionMode::Cut : InjectionMode::Fuse;
	markVolumeDirty();
}

void SingleParticleWorkspace::cycleMirrorMode(int direction) {
	if (direction == 0) return;
	m_mirrorMode = m_mirrorMode == MirrorMode::On
		? MirrorMode::None : MirrorMode::On;
	markVolumeDirty();
}

void SingleParticleWorkspace::cycleOffsetVector(int direction) {
	m_offsetVector = cycleEnum(m_offsetVector,
		static_cast<int>(OffsetVector::Count), direction);
}

void SingleParticleWorkspace::cycleOffsetIncrement(int direction) {
	static constexpr float values[] = {
		0.01f, 0.012f, 0.02f, 0.025f, 0.05f,
		0.10f, 0.12f, 0.20f, 0.25f, 0.50f
	};
	constexpr int count = static_cast<int>(sizeof(values) / sizeof(values[0]));
	if (direction < 0) m_offsetIncrementIndex =
		(m_offsetIncrementIndex + count - 1) % count;
	else if (direction > 0) m_offsetIncrementIndex =
		(m_offsetIncrementIndex + 1) % count;
	m_offsetIncrement = values[m_offsetIncrementIndex];
}

void SingleParticleWorkspace::cycleRotationIncrement(int direction) {
	constexpr int count = 5;
	if (direction < 0) m_rotationIncrementIndex =
		(m_rotationIncrementIndex + count - 1) % count;
	else if (direction > 0) m_rotationIncrementIndex =
		(m_rotationIncrementIndex + 1) % count;
}

void SingleParticleWorkspace::adjustInjectionRail(int direction) {
	if (direction == 0) return;
	m_injectionRailT += (direction < 0 ? -1.0f : 1.0f) * m_offsetIncrement;
	m_injectionRailT = (std::max)(0.0f, (std::min)(1.0f, m_injectionRailT));
	m_injectionRailT = std::round(m_injectionRailT * 1000000.0f) / 1000000.0f;
	markVolumeDirty();
}

void SingleParticleWorkspace::adjustObjectOffset(int direction) {
	if (direction == 0) return;
	VolumeObjectState& state = activeVolumeState();
	float* value = m_offsetVector == OffsetVector::Y ? &state.offsetY
		: m_offsetVector == OffsetVector::Z ? &state.offsetZ : &state.offsetX;
	*value += (direction < 0 ? -1.0f : 1.0f) * m_offsetIncrement;
	*value = std::round(*value * 1000000.0f) / 1000000.0f;
	if (std::fabs(*value) < 0.0000005f) *value = 0.0f;
	markVolumeDirty();
}

void SingleParticleWorkspace::adjustObjectScale(int direction) {
	if (direction == 0) return;
	VolumeObjectState& state = activeVolumeState();
	float* value = &state.scaleWhole;
	if (m_objectEditMode == ObjectEditMode::ScaleX) value = &state.scaleX;
	else if (m_objectEditMode == ObjectEditMode::ScaleY) value = &state.scaleY;
	else if (m_objectEditMode == ObjectEditMode::ScaleZ) value = &state.scaleZ;
	*value += direction < 0 ? -0.05f : 0.05f;
	*value = (std::max)(0.10f, *value);
	markVolumeDirty();
}

void SingleParticleWorkspace::adjustObjectRotation(int direction) {
	if (direction == 0) return;
	static constexpr float increments[] = { 1.0f, 20.0f, 45.0f, 90.0f, 180.0f };
	const float delta = increments[m_rotationIncrementIndex] *
		(direction < 0 ? -1.0f : 1.0f);
	VolumeObjectState& state = activeVolumeState();
	if (m_objectRotationMode == ObjectRotationMode::Yaw) state.yawDeg += delta;
	else if (m_objectRotationMode == ObjectRotationMode::Roll) state.rollDeg += delta;
	else state.pitchDeg += delta;
	markVolumeDirty();
}

void SingleParticleWorkspace::resetObjectOffset() {
	VolumeObjectState& state = activeVolumeState();
	state.offsetX = state.offsetY = state.offsetZ = 0.0f;
	markVolumeDirty();
}

void SingleParticleWorkspace::resetObjectScale() {
	VolumeObjectState& state = activeVolumeState();
	state.scaleWhole = state.scaleX = state.scaleY = state.scaleZ = 1.0f;
	markVolumeDirty();
}

void SingleParticleWorkspace::resetObjectRotation() {
	VolumeObjectState& state = activeVolumeState();
	state.pitchDeg = state.yawDeg = state.rollDeg = 0.0f;
	markVolumeDirty();
}

void SingleParticleWorkspace::commitObjectRotationToBasis() {
	VolumeObjectState& state = activeVolumeState();
	state.basis = getEffectiveObjectBasis();
	state.pitchDeg = state.yawDeg = state.rollDeg = 0.0f;
	m_objectTransformMode = ObjectTransformMode::Scale;
	markVolumeDirty();
}

void SingleParticleWorkspace::commitBrushBase() {
	if (!hasInjectionVoxelSelected() || !isEditingInjectionVoxel1()) {
		commitObjectRotationToBasis();
		return;
	}
	VolumeObjectState& state = m_volume1State;
	if (state.primitive != VOLUME_PRIMITIVE_BASE)
		state.brushBasePrimitive = state.primitive;
	else if (state.brushBasePrimitive == VOLUME_PRIMITIVE_BASE)
		state.brushBasePrimitive = VOLUME_PRIMITIVE_SPHERE;
	commitObjectRotationToBasis();
	state.primitive = VOLUME_PRIMITIVE_BASE;
	state.brushBaseReady = true;
	m_objectTransformMode = ObjectTransformMode::Scale;
	markVolumeDirty();
}

bool SingleParticleWorkspace::canApplyVolumeToBase() const {
	if (hasInjectionVoxelSelected() && m_injectionMode == InjectionMode::Cut)
		return true;
	return isSPVolumeBoundarySafe();
}

SingleParticleWorkspace::SPVolumeBasis
SingleParticleWorkspace::buildSPVolumeBasis(
	const SingleParticleWorkspace& workspace) const {
	const ObjectBasis basis = workspace.getEffectiveObjectBasis();
	return {
		make_float3(basis.xAxis.x, basis.xAxis.y, basis.xAxis.z),
		make_float3(basis.yAxis.x, basis.yAxis.y, basis.yAxis.z),
		make_float3(basis.zAxis.x, basis.zAxis.y, basis.zAxis.z)
	};
}

SingleParticleWorkspace::SPVolumeBasis
SingleParticleWorkspace::buildSPVolumeBasisFromState(
	const SingleParticleWorkspace& workspace,
	const VolumeObjectState& state) const {
	const BasisVector localX = workspace.rotateLocalVectorXYZ(
		{ 1.0f, 0.0f, 0.0f }, state.pitchDeg, state.yawDeg, state.rollDeg);
	const BasisVector localY = workspace.rotateLocalVectorXYZ(
		{ 0.0f, 1.0f, 0.0f }, state.pitchDeg, state.yawDeg, state.rollDeg);
	const BasisVector localZ = workspace.rotateLocalVectorXYZ(
		{ 0.0f, 0.0f, 1.0f }, state.pitchDeg, state.yawDeg, state.rollDeg);
	ObjectBasis basis{
		workspace.transformByBasis(state.basis, localX),
		workspace.transformByBasis(state.basis, localY),
		workspace.transformByBasis(state.basis, localZ)
	};
	basis = workspace.orthonormalizeBasis(basis);
	return {
		make_float3(basis.xAxis.x, basis.xAxis.y, basis.xAxis.z),
		make_float3(basis.yAxis.x, basis.yAxis.y, basis.yAxis.z),
		make_float3(basis.zAxis.x, basis.zAxis.y, basis.zAxis.z)
	};
}

void SingleParticleWorkspace::regenerateSPVolumeField(
	const SingleParticleWorkspace& workspace) {
	m_volumeBoundarySensorReady = false;
	if (!m_dWorkingVolume) return;
	if (!m_dBaseVolume) {
		if (!workspace.hasEditableVolumePrimitive())
			clearVolumeKernelLauncher(m_dWorkingVolume, m_volumeSize, 1.0e6f);
		else {
			const SPVolumeBasis basis = buildSPVolumeBasis(workspace);
			volumeKernelLauncher(m_dWorkingVolume, m_volumeSize,
				getSPVolumePrimitiveId(workspace),
				buildSPVolumePrimitiveParams(workspace),
				buildSPVolumeOffset(workspace),
				basis.xAxis, basis.yAxis, basis.zAxis);
		}
		threadSync();
	}
	else if (workspace.getVolumeAssemblyNode() == VOLUME_NODE_PREVIEW &&
		m_hasCommittedGeometry) {
		copyCommittedVolumeToPreview();
	}
	else {
		updateSPVolumePreview(workspace);
	}
	m_volumeDirty = false;
	updateSPVolumeBoundarySensor(0.0f, 0.0f);
}

void SingleParticleWorkspace::renderSPVolumeOrientationAxes(
	const SingleParticleWorkspace& workspace,
	float thetaRad,
	float phiRad) {
	if (!m_renderer) return;
	const ObjectBasis baked = workspace.getObjectBasis();
	const ObjectBasis effective = workspace.getEffectiveObjectBasis();
	EuclidRenderer::VolumeObjectBasis rendererBaked{
		glm::vec3(baked.xAxis.x, baked.xAxis.y, baked.xAxis.z),
		glm::vec3(baked.yAxis.x, baked.yAxis.y, baked.yAxis.z),
		glm::vec3(baked.zAxis.x, baked.zAxis.y, baked.zAxis.z)
	};
	EuclidRenderer::VolumeObjectBasis rendererEffective{
		glm::vec3(effective.xAxis.x, effective.xAxis.y, effective.xAxis.z),
		glm::vec3(effective.yAxis.x, effective.yAxis.y, effective.yAxis.z),
		glm::vec3(effective.zAxis.x, effective.zAxis.y, effective.zAxis.z)
	};
	EuclidRenderer::VolumeAxisGuideMode guide =
		EuclidRenderer::VOLUME_AXIS_GUIDE_DEFAULT;
	const bool editNode = workspace.isVolumeRenderSubLayer() &&
		workspace.getVolumeAssemblyNode() == VOLUME_NODE_EDIT_OBJECT;
	if (editNode && workspace.getObjectTransformMode() == TRANSFORM_SCALE) {
		if (workspace.getObjectEditMode() == EDIT_SCALE_X)
			guide = EuclidRenderer::VOLUME_AXIS_GUIDE_X;
		else if (workspace.getObjectEditMode() == EDIT_SCALE_Y)
			guide = EuclidRenderer::VOLUME_AXIS_GUIDE_Y;
		else if (workspace.getObjectEditMode() == EDIT_SCALE_Z)
			guide = EuclidRenderer::VOLUME_AXIS_GUIDE_Z;
	}
	else if (editNode && workspace.getObjectTransformMode() == TRANSFORM_ROTATION) {
		if (workspace.getObjectRotationMode() == ROTATE_YAW)
			guide = EuclidRenderer::VOLUME_AXIS_GUIDE_YAW;
		else if (workspace.getObjectRotationMode() == ROTATE_ROLL)
			guide = EuclidRenderer::VOLUME_AXIS_GUIDE_ROLL;
		else guide = EuclidRenderer::VOLUME_AXIS_GUIDE_PITCH;
	}
	m_renderer->displayVolumeOrientationAxes(thetaRad, phiRad, guide,
		rendererBaked, rendererEffective,
		workspace.getRotationPitchDeg(), workspace.getRotationYawDeg(),
		workspace.getRotationRollDeg());
}

void SingleParticleWorkspace::renderSPVolumeInjectionVoxelPreview(
	const SingleParticleWorkspace& workspace,
	float thetaRad,
	float phiRad,
	float zs) {
	if (!m_renderer) return;
	const bool selected = workspace.isVolumeRenderSubLayer() &&
		workspace.getVolumeAssemblyNode() == VOLUME_NODE_PREVIEW &&
		workspace.isSubLayerPanelOpen() &&
		workspace.getActiveSubLayerPanelItem() == PREVIEW_LIST_INJECTION_MODE;
	if (!selected) return;
	m_renderer->displayVolumeInjectionVoxelPreview(thetaRad, phiRad, zs,
		m_volumeSize.x, 0.55f, workspace.getInjectionVoxelDX(),
		workspace.getInjectionVoxelDY(), workspace.getInjectionVoxelDZ());
}

void SingleParticleWorkspace::renderSPVolumeInjectionEditTargetPreview(
	const SingleParticleWorkspace& workspace,
	float thetaRad,
	float phiRad,
	float zs) {
	if (!m_renderer) return;
	const bool overlap = isSPOverlapPreviewActive();
	const bool targetSelected = workspace.isVolumeRenderSubLayer() &&
		workspace.getVolumeAssemblyNode() == VOLUME_NODE_EDIT_OBJECT &&
		workspace.hasInjectionVoxelSelected() && workspace.isSubLayerPanelOpen() &&
		workspace.getActiveSubLayerPanelItem() == INJECTION_EDIT_LIST_TARGET;
	if (!overlap && !targetSelected && !workspace.isSPMirrorEnabled()) return;
	m_renderer->displayVolumeInjectionEditTargetPreview(thetaRad, phiRad, zs,
		m_volumeSize.x, 0.55f, workspace.getInjectionVoxelDX(),
		workspace.getInjectionVoxelDY(), workspace.getInjectionVoxelDZ(),
		workspace.isEditingInjectionVoxel1(), overlap);
	if (workspace.isSPMirrorEnabled()) {
		m_renderer->displaySPMirrorGuides(thetaRad, phiRad, zs, m_volumeSize.x,
			workspace.getInjectionVoxelDX(), workspace.getInjectionVoxelDY(),
			workspace.getInjectionVoxelDZ(), workspace.isEditingInjectionVoxel1(),
			overlap, workspace.getInjectionRailT(),
			workspace.getVolume1State().offsetX * 0.5f * m_volumeSize.x,
			workspace.getVolume1State().offsetY * 0.5f * m_volumeSize.y,
			workspace.getVolume1State().offsetZ * 0.5f * m_volumeSize.z);
	}
}

void SingleParticleWorkspace::renderSPVolumeTexture() {
	if (m_renderer) m_renderer->displayVolumeTexture();
}

void SingleParticleWorkspace::renderSPVolumeOffsetGrid(
	const SingleParticleWorkspace& workspace,
	float thetaRad,
	float phiRad,
	float zs) {
	if (!m_renderer || !workspace.isVolumeRenderSubLayer() ||
		workspace.getVolumeAssemblyNode() != VOLUME_NODE_OFFSET_OBJECT) return;

	const bool overlap = isSPOverlapPreviewActive();
	const bool targetSelected = workspace.hasInjectionVoxelSelected() &&
		workspace.isSubLayerPanelOpen() &&
		workspace.getActiveSubLayerPanelItem() == INJECTION_OFFSET_LIST_TARGET;
	if (overlap || targetSelected) {
		m_renderer->displayVolumeInjectionEditTargetPreview(
			thetaRad, phiRad, zs, m_volumeSize.x,
			overlap ? 0.55f : 0.48f,
			workspace.getInjectionVoxelDX(), workspace.getInjectionVoxelDY(),
			workspace.getInjectionVoxelDZ(), workspace.isEditingInjectionVoxel1(), overlap);
		if (workspace.isSPMirrorEnabled()) {
			m_renderer->displaySPMirrorGuides(thetaRad, phiRad, zs, m_volumeSize.x,
				workspace.getInjectionVoxelDX(), workspace.getInjectionVoxelDY(),
				workspace.getInjectionVoxelDZ(), workspace.isEditingInjectionVoxel1(),
				overlap, workspace.getInjectionRailT(),
				workspace.getVolume1State().offsetX * 0.5f * m_volumeSize.x,
				workspace.getVolume1State().offsetY * 0.5f * m_volumeSize.y,
				workspace.getVolume1State().offsetZ * 0.5f * m_volumeSize.z);
		}
		return;
	}

	EuclidRenderer::VolumeOffsetAxis axis = EuclidRenderer::VOLUME_OFFSET_AXIS_X;
	if (workspace.getOffsetVectorSelection() == OFFSET_VECTOR_Y)
		axis = EuclidRenderer::VOLUME_OFFSET_AXIS_Y;
	else if (workspace.getOffsetVectorSelection() == OFFSET_VECTOR_Z)
		axis = EuclidRenderer::VOLUME_OFFSET_AXIS_Z;
	m_renderer->displayVolumeOffsetGrid(thetaRad, phiRad, zs, m_volumeSize.x,
		axis, workspace.getOffsetIncrement(), workspace.getOffsetX(),
		workspace.getOffsetY(), workspace.getOffsetZ(),
		m_volumeBoundaryMaskCPU, m_volumeBoundaryFaceStride);

	const bool railSelected = workspace.hasInjectionVoxelSelected() &&
		workspace.isEditingInjectionVoxel0() && workspace.isSubLayerPanelOpen() &&
		workspace.getActiveSubLayerPanelItem() == INJECTION_OFFSET_LIST_RAIL;
	if (railSelected) {
		const VolumeObjectState& brush = workspace.getVolume1State();
		m_renderer->displayVolumeInjectionRailMarker(thetaRad, phiRad, zs,
			m_volumeSize.x, workspace.getInjectionVoxelDX(),
			workspace.getInjectionVoxelDY(), workspace.getInjectionVoxelDZ(),
			workspace.getInjectionRailT(), 1.0f,
			brush.offsetX * 0.5f * m_volumeSize.x,
			brush.offsetY * 0.5f * m_volumeSize.y,
			brush.offsetZ * 0.5f * m_volumeSize.z);
	}
	if (workspace.isSPMirrorEnabled()) {
		m_renderer->displaySPMirrorGuides(thetaRad, phiRad, zs, m_volumeSize.x,
			workspace.getInjectionVoxelDX(), workspace.getInjectionVoxelDY(),
			workspace.getInjectionVoxelDZ(), workspace.isEditingInjectionVoxel1(),
			false, workspace.getInjectionRailT(),
			workspace.getVolume1State().offsetX * 0.5f * m_volumeSize.x,
			workspace.getVolume1State().offsetY * 0.5f * m_volumeSize.y,
			workspace.getVolume1State().offsetZ * 0.5f * m_volumeSize.z);
	}
}

int SingleParticleWorkspace::getSPVolumePrimitiveId(
	const SingleParticleWorkspace& workspace) const {
	switch (workspace.getResolvedVolumePrimitiveSelection()) {
	case VOLUME_PRIMITIVE_BASE: return -1;
	case VOLUME_PRIMITIVE_SPHERE: return 0;
	case VOLUME_PRIMITIVE_TORUS: return 1;
	case VOLUME_PRIMITIVE_BLOCK: return 2;
	case VOLUME_PRIMITIVE_CYLINDER: return 3;
	case VOLUME_PRIMITIVE_CAPSULE: return 4;
	case VOLUME_PRIMITIVE_WEDGE: return 5;
	case VOLUME_PRIMITIVE_FRUSTUM: return 6;
	case VOLUME_PRIMITIVE_CONE: return 7;
	case VOLUME_PRIMITIVE_DELTA_WING: return 8;
	default: return -1;
	}
}

int SingleParticleWorkspace::getSPVolumePrimitiveIdFromState(
	const VolumeObjectState& state) const {
	VolumePrimitive primitive = state.primitive;
	if (primitive == VOLUME_PRIMITIVE_BASE) {
		primitive = state.brushBaseReady &&
			state.brushBasePrimitive != VOLUME_PRIMITIVE_BASE
			? state.brushBasePrimitive : VOLUME_PRIMITIVE_SPHERE;
	}
	switch (primitive) {
	case VOLUME_PRIMITIVE_SPHERE: return 0;
	case VOLUME_PRIMITIVE_TORUS: return 1;
	case VOLUME_PRIMITIVE_BLOCK: return 2;
	case VOLUME_PRIMITIVE_CYLINDER: return 3;
	case VOLUME_PRIMITIVE_CAPSULE: return 4;
	case VOLUME_PRIMITIVE_WEDGE: return 5;
	case VOLUME_PRIMITIVE_FRUSTUM: return 6;
	case VOLUME_PRIMITIVE_CONE: return 7;
	case VOLUME_PRIMITIVE_DELTA_WING: return 8;
	default: return 0;
	}
}

float3 SingleParticleWorkspace::buildSPVolumeOffset(
	const SingleParticleWorkspace& workspace) const {
	return make_float3(
		workspace.getOffsetX() * 0.5f * m_volumeSize.x,
		workspace.getOffsetY() * 0.5f * m_volumeSize.y,
		workspace.getOffsetZ() * 0.5f * m_volumeSize.z);
}

float3 SingleParticleWorkspace::buildSPVolumeOffsetFromState(
	const VolumeObjectState& state) const {
	return make_float3(
		state.offsetX * 0.5f * m_volumeSize.x,
		state.offsetY * 0.5f * m_volumeSize.y,
		state.offsetZ * 0.5f * m_volumeSize.z);
}

float3 SingleParticleWorkspace::buildSPVolumeRailBrushOffset(
	const SingleParticleWorkspace& workspace) const {
	const VolumeObjectState& brush = workspace.getVolume1State();
	const float railT = (std::max)(0.0f,
		(std::min)(1.0f, workspace.getInjectionRailT()));
	const float3 center = make_float3(
		workspace.getInjectionVoxelDX() * static_cast<float>(m_volumeSize.x),
		workspace.getInjectionVoxelDY() * static_cast<float>(m_volumeSize.y),
		workspace.getInjectionVoxelDZ() * static_cast<float>(m_volumeSize.z));
	const float3 local = buildSPVolumeOffsetFromState(brush);
	return make_float3(center.x * (1.0f - railT) + local.x,
		center.y * (1.0f - railT) + local.y,
		center.z * (1.0f - railT) + local.z);
}

float4 SingleParticleWorkspace::buildSPVolumePrimitiveParams(
	const SingleParticleWorkspace& workspace) const {
	VolumeObjectState state = workspace.getActiveVolumeState();
	state.primitive = workspace.getResolvedVolumePrimitiveSelection();
	return buildSPVolumePrimitiveParamsFromState(state);
}

float4 SingleParticleWorkspace::buildSPVolumePrimitiveParamsFromState(
	const VolumeObjectState& state) const {
	const float minDim = static_cast<float>((std::min)(m_volumeSize.x,
		(std::min)(m_volumeSize.y, m_volumeSize.z)));
	const float sx = state.scaleWhole * state.scaleX;
	const float sy = state.scaleWhole * state.scaleY;
	const float sz = state.scaleWhole * state.scaleZ;
	VolumePrimitive primitive = state.primitive;
	if (primitive == VOLUME_PRIMITIVE_BASE)
		primitive = state.brushBaseReady &&
			state.brushBasePrimitive != VOLUME_PRIMITIVE_BASE
			? state.brushBasePrimitive : VOLUME_PRIMITIVE_SPHERE;
	float4 param = make_float4(0.0f, 0.0f, 0.0f, 0.0f);
	switch (primitive) {
	case VOLUME_PRIMITIVE_TORUS:
		param.x = minDim * 0.30f * (0.5f * (sx + sy));
		param.y = minDim * 0.12f * sz;
		break;
	case VOLUME_PRIMITIVE_BLOCK:
	case VOLUME_PRIMITIVE_WEDGE:
		param.x = minDim * 0.42f * sx;
		param.y = minDim * 0.42f * sy;
		param.z = minDim * 0.42f * sz;
		break;
	case VOLUME_PRIMITIVE_CYLINDER:
		param.x = minDim * 0.32f * (0.5f * (sx + sy));
		param.z = minDim * 0.42f * sz;
		break;
	case VOLUME_PRIMITIVE_CAPSULE:
		param.x = minDim * 0.20f * (0.5f * (sx + sy));
		param.z = minDim * 0.26f * sz;
		break;
	case VOLUME_PRIMITIVE_CONE:
		param.x = minDim * 0.42f * (0.5f * (sx + sy));
		param.z = minDim * 0.46f * sz;
		break;
	case VOLUME_PRIMITIVE_DELTA_WING:
		param.x = minDim * 0.46f * sx;
		param.y = minDim * 0.46f * sy;
		param.z = minDim * 0.10f * sz;
		break;
	case VOLUME_PRIMITIVE_FRUSTUM:
		param.x = minDim * 0.42f * sx;
		param.y = minDim * 0.42f * sy;
		param.z = minDim * 0.42f * sz;
		param.w = 0.45f;
		break;
	default:
	case VOLUME_PRIMITIVE_SPHERE:
		param.x = minDim * 0.46f * sx;
		param.y = minDim * 0.46f * sy;
		param.z = minDim * 0.46f * sz;
		break;
	}
	return param;
}

void SingleParticleWorkspace::generateSPVolume0Field(
	const SingleParticleWorkspace& workspace,
	float* destination) {
	if (!destination) return;
	const VolumeObjectState& state = workspace.getVolume0State();
	const SPVolumeBasis basis = buildSPVolumeBasisFromState(workspace, state);
	const float3 offset = buildSPVolumeOffsetFromState(state);
	if (state.primitive == VOLUME_PRIMITIVE_BASE) {
		if (!m_hasCommittedGeometry || !m_dBaseVolume)
			clearVolumeKernelLauncher(destination, m_volumeSize, 1.0e6f);
		else
			transformVolumeFieldLauncher(m_dBaseVolume, destination, m_volumeSize,
				make_float3(state.scaleWhole * state.scaleX,
					state.scaleWhole * state.scaleY,
					state.scaleWhole * state.scaleZ),
				offset, basis.xAxis, basis.yAxis, basis.zAxis);
		return;
	}
	volumeKernelLauncher(destination, m_volumeSize,
		getSPVolumePrimitiveIdFromState(state),
		buildSPVolumePrimitiveParamsFromState(state), offset,
		basis.xAxis, basis.yAxis, basis.zAxis);
}

void SingleParticleWorkspace::generateSPVolume1BrushField(
	const SingleParticleWorkspace& workspace,
	float* destination) {
	if (!destination) return;
	if (!workspace.hasInjectionVoxelSelected()) {
		clearVolumeKernelLauncher(destination, m_volumeSize, 1.0e6f);
		return;
	}
	const VolumeObjectState& state = workspace.getVolume1State();
	const SPVolumeBasis basis = buildSPVolumeBasisFromState(workspace, state);
	volumeKernelLauncher(destination, m_volumeSize,
		getSPVolumePrimitiveIdFromState(state),
		buildSPVolumePrimitiveParamsFromState(state),
		buildSPVolumeRailBrushOffset(workspace),
		basis.xAxis, basis.yAxis, basis.zAxis);
}

void SingleParticleWorkspace::generateSPVolume1MirroredBrushField(
	const SingleParticleWorkspace& workspace,
	float* destination) {
	if (!destination) return;
	if (!workspace.isSPMirrorEnabled()) {
		clearVolumeKernelLauncher(destination, m_volumeSize, 1.0e6f);
		return;
	}
	const VolumeObjectState& state = workspace.getVolume1State();
	const SPVolumeBasis basis = buildSPVolumeBasisFromState(workspace, state);
	const float3 railBrushOffset = buildSPVolumeRailBrushOffset(workspace);
	int dx = 0;
	int dy = 0;
	int dz = 0;
	workspace.getMirroredInjectionDirection(dx, dy, dz);
	const float3 planeNormal = make_float3(
		static_cast<float>(-dx), static_cast<float>(-dy),
		static_cast<float>(-dz));
	mirroredVolumeKernelLauncher(destination, m_volumeSize,
		getSPVolumePrimitiveIdFromState(state),
		buildSPVolumePrimitiveParamsFromState(state), railBrushOffset,
		basis.xAxis, basis.yAxis, basis.zAxis, planeNormal);
}

void SingleParticleWorkspace::updateSPVolumePreview(
	const SingleParticleWorkspace& workspace) {
	m_volumeBoundarySensorReady = false;
	if (!m_dWorkingVolume || !m_dBaseVolume) return;
	if (!m_committedVolumeReady) clearSPCommittedVolume();
	const SPVolumeBasis basis = buildSPVolumeBasis(workspace);
	const bool baseSelected = workspace.getVolumePrimitiveSelection() ==
		VOLUME_PRIMITIVE_BASE && !workspace.isInjectionBrushBaseSelected();
	if (baseSelected) {
		if (!m_hasCommittedGeometry)
			clearVolumeKernelLauncher(m_dWorkingVolume, m_volumeSize, 1.0e6f);
		else
			transformVolumeFieldLauncher(m_dBaseVolume, m_dWorkingVolume,
				m_volumeSize, make_float3(workspace.getEffectiveVolumeScaleX(),
					workspace.getEffectiveVolumeScaleY(),
					workspace.getEffectiveVolumeScaleZ()),
				buildSPVolumeOffset(workspace), basis.xAxis, basis.yAxis, basis.zAxis);
		threadSync();
		m_volumeDirty = false;
		return;
	}
	volumeKernelLauncher(m_dWorkingVolume, m_volumeSize,
		getSPVolumePrimitiveId(workspace), buildSPVolumePrimitiveParams(workspace),
		buildSPVolumeOffset(workspace), basis.xAxis, basis.yAxis, basis.zAxis);
	threadSync();
	m_volumeDirty = false;
}

void SingleParticleWorkspace::copyCommittedVolumeToPreview() {
	m_volumeBoundarySensorReady = false;
	if (!m_dBaseVolume || !m_dWorkingVolume) return;
	cudaMemcpy(m_dWorkingVolume, m_dBaseVolume, getVolumeBytes(),
		cudaMemcpyDeviceToDevice);
	threadSync();
	m_volumeDirty = false;
}

void SingleParticleWorkspace::clearSPCommittedVolume() {
	if (!m_dBaseVolume) return;
	clearVolumeKernelLauncher(m_dBaseVolume, m_volumeSize, 1.0e6f);
	threadSync();
	m_committedVolumeReady = true;
	m_hasCommittedGeometry = false;
	m_volumeDirty = true;
	m_volumeBoundarySensorReady = false;
}

bool SingleParticleWorkspace::initializeSPVolumeBoundarySensor() {
	if (m_volumeSize.x <= 0 || m_volumeSize.y <= 0 || m_volumeSize.z <= 0)
		return false;
	const unsigned int faceStride = getVolumeBoundaryFaceStride(m_volumeSize);
	const std::size_t maskBytes = getVolumeBoundaryMaskBytes(m_volumeSize);
	if (faceStride == 0 || maskBytes == 0) return false;
	const bool valid = m_dVolumeBoundaryMask && m_dVolumeBoundaryUnsafeCount &&
		m_dVolumeInsideSampleCount && m_volumeBoundaryFaceStride == faceStride &&
		m_volumeBoundaryMaskCPU.size() == maskBytes;
	if (valid) return true;
	releaseSPVolumeBoundarySensor();
	allocateArray(reinterpret_cast<void**>(&m_dVolumeBoundaryMask), maskBytes);
	allocateArray(reinterpret_cast<void**>(&m_dVolumeBoundaryUnsafeCount),
		sizeof(unsigned int));
	allocateArray(reinterpret_cast<void**>(&m_dVolumeInsideSampleCount),
		sizeof(unsigned int));
	if (!m_dVolumeBoundaryMask || !m_dVolumeBoundaryUnsafeCount ||
		!m_dVolumeInsideSampleCount) {
		releaseSPVolumeBoundarySensor();
		return false;
	}
	m_volumeBoundaryMaskCPU.assign(maskBytes, static_cast<unsigned char>(0));
	m_volumeBoundaryFaceStride = faceStride;
	m_volumeBoundaryUnsafeCount = 0;
	m_volumeBoundarySensorReady = false;
	return true;
}

bool SingleParticleWorkspace::updateSPVolumeBoundarySensor(
	float isoValue,
	float safetyBand) {
	return classifySPVolumeBoundaryForSource(
		m_dWorkingVolume, isoValue, safetyBand);
}

bool SingleParticleWorkspace::classifySPVolumeBoundaryForSource(
	const float* source,
	float isoValue,
	float safetyBand) {
	return classifySPVolumeBoundaryForSource(source, isoValue, safetyBand,
		m_volumeBoundarySensorReady, m_volumeBoundaryUnsafeCount,
		nullptr, &m_volumeBoundaryMaskCPU);
}

bool SingleParticleWorkspace::classifySPVolumeBoundaryForSource(
	const float* source,
	float isoValue,
	float safetyBand,
	bool& sensorReady,
	unsigned int& unsafeCount,
	unsigned int* insideSampleCount,
	std::vector<unsigned char>* boundaryMaskCPU) {
	sensorReady = false;
	unsafeCount = 0;
	if (insideSampleCount) *insideSampleCount = 0;
	if (!source || !initializeSPVolumeBoundarySensor()) return false;
	const std::size_t maskBytes = getVolumeBoundaryMaskBytes(m_volumeSize);
	if (maskBytes == 0) return false;
	if (boundaryMaskCPU && boundaryMaskCPU->size() != maskBytes)
		boundaryMaskCPU->assign(maskBytes, static_cast<unsigned char>(0));
	classifyVolumeBoundaryLauncher(source, m_dVolumeBoundaryMask,
		m_dVolumeBoundaryUnsafeCount,
		insideSampleCount ? m_dVolumeInsideSampleCount : nullptr,
		m_volumeSize, isoValue, safetyBand);
	threadSync();
	copyArrayFromDevice(&unsafeCount, m_dVolumeBoundaryUnsafeCount, nullptr,
		static_cast<int>(sizeof(unsigned int)));
	if (insideSampleCount)
		copyArrayFromDevice(insideSampleCount, m_dVolumeInsideSampleCount, nullptr,
			static_cast<int>(sizeof(unsigned int)));
	if (boundaryMaskCPU && unsafeCount == 0)
		std::fill(boundaryMaskCPU->begin(), boundaryMaskCPU->end(),
			static_cast<unsigned char>(0));
	else if (boundaryMaskCPU)
		copyArrayFromDevice(boundaryMaskCPU->data(), m_dVolumeBoundaryMask,
			nullptr, static_cast<int>(maskBytes));
	sensorReady = true;
	return true;
}

void SingleParticleWorkspace::updateSPOverlapPreviewStatus(
	const SingleParticleWorkspace& workspace) {
	clearSPOverlapPreviewStatus();
	const bool context = workspace.isVolumeRenderSubLayer() &&
		workspace.hasInjectionVoxelSelected() && m_dBrushVolume &&
		(workspace.getVolumeAssemblyNode() == VOLUME_NODE_EDIT_OBJECT ||
			workspace.getVolumeAssemblyNode() == VOLUME_NODE_OFFSET_OBJECT);
	if (!context) return;
	generateSPVolume1BrushField(workspace, m_dBrushVolume);
	if (workspace.isSPMirrorEnabled())
		generateSPVolume1MirroredBrushField(workspace, m_dMirrorBrushVolume);
	threadSync();
	classifySPVolumeBoundaryForSource(m_dBrushVolume, 0.0f, 0.0f,
		m_spOverlapPreviewSensorReady, m_spOverlapPreviewUnsafeCount,
		&m_spOverlapPreviewInsideSampleCount, nullptr);
	m_spOverlapPreviewMirrorRequired = workspace.isSPMirrorEnabled();
	if (m_spOverlapPreviewMirrorRequired && m_dMirrorBrushVolume) {
		generateSPVolume1MirroredBrushField(workspace, m_dMirrorBrushVolume);
		threadSync();
		classifySPVolumeBoundaryForSource(m_dMirrorBrushVolume, 0.0f, 0.0f,
			m_spMirrorOverlapPreviewSensorReady,
			m_spMirrorOverlapPreviewUnsafeCount,
			&m_spMirrorOverlapPreviewInsideSampleCount, nullptr);
	}
}

void SingleParticleWorkspace::clearSPOverlapPreviewStatus() {
	m_spOverlapPreviewSensorReady = false;
	m_spOverlapPreviewUnsafeCount = 0;
	m_spOverlapPreviewInsideSampleCount = 0;
	m_spMirrorOverlapPreviewSensorReady = false;
	m_spMirrorOverlapPreviewUnsafeCount = 0;
	m_spMirrorOverlapPreviewInsideSampleCount = 0;
	m_spOverlapPreviewMirrorRequired = false;
}

void SingleParticleWorkspace::releaseSPVolumeBoundarySensor() {
	if (m_dVolumeBoundaryMask) freeArray(m_dVolumeBoundaryMask);
	if (m_dVolumeBoundaryUnsafeCount) freeArray(m_dVolumeBoundaryUnsafeCount);
	if (m_dVolumeInsideSampleCount) freeArray(m_dVolumeInsideSampleCount);
	m_dVolumeBoundaryMask = nullptr;
	m_dVolumeBoundaryUnsafeCount = nullptr;
	m_dVolumeInsideSampleCount = nullptr;
	m_volumeBoundaryMaskCPU.clear();
	m_volumeBoundaryFaceStride = 0;
	m_volumeBoundaryUnsafeCount = 0;
	m_volumeBoundarySensorReady = false;
	clearSPOverlapPreviewStatus();
}

void SingleParticleWorkspace::markSPVolumeBoundarySafe() {
	m_volumeBoundarySensorReady = true;
	m_volumeBoundaryUnsafeCount = 0;
	std::fill(m_volumeBoundaryMaskCPU.begin(), m_volumeBoundaryMaskCPU.end(),
		static_cast<unsigned char>(0));
}

bool SingleParticleWorkspace::renderSPVolumeToPBO(
	const SingleParticleWorkspace& workspace,
	int renderMethod,
	int viewportW,
	int viewportH,
	float thetaRad,
	float phiRad,
	float zs,
	float threshold,
	float sliceDistance) {
	if (!m_dWorkingVolume || !m_cudaPboResourceSlot ||
		!(*m_cudaPboResourceSlot)) return false;

	updateSPOverlapPreviewStatus(workspace);
	const bool sharedOverlap = isSPOverlapPreviewActive();
	const bool legacyRailPreview = workspace.hasInjectionVoxelSelected() &&
		m_dBrushVolume && workspace.isVolumeRenderSubLayer() &&
		workspace.getVolumeAssemblyNode() == VOLUME_NODE_OFFSET_OBJECT &&
		workspace.isEditingInjectionVoxel0();
	const bool twoPass = sharedOverlap || legacyRailPreview;
	if (twoPass) {
		generateSPVolume0Field(workspace, m_dWorkingVolume);
		if (legacyRailPreview) {
			if (workspace.getVolumeInjectionMode() == VOLUME_CUT)
				markSPVolumeBoundarySafe();
			else classifySPVolumeBoundaryForSource(
				m_dBrushVolume, 0.0f, 0.0f);
		}
		m_volumeDirty = false;
	}
	else if (m_volumeDirty) {
		regenerateSPVolumeField(workspace);
	}

	uchar4* output = reinterpret_cast<uchar4*>(
		mapGLBufferObject(m_cudaPboResourceSlot));
	if (!output) return false;
	float mainR = 1.0f, mainG = 0.0f, mainB = 0.0f;
	if (sharedOverlap && workspace.isEditingInjectionVoxel1()) {
		mainR = 0.34f; mainG = 0.025f; mainB = 0.015f;
	}
	const bool standaloneCut = !twoPass && workspace.hasInjectionVoxelSelected() &&
		workspace.isEditingInjectionVoxel1() &&
		workspace.getVolumeInjectionMode() == VOLUME_CUT;
	if (standaloneCut) {
		mainR = 0.05f; mainG = 0.28f; mainB = 1.0f;
	}
	kernelLauncher(output, m_dWorkingVolume, viewportW, viewportH,
		m_volumeSize, renderMethod, zs, thetaRad, phiRad, threshold,
		sliceDistance, mainR, mainG, mainB);

	if (twoPass) {
		float brushR = 1.0f, brushG = 0.0f, brushB = 0.0f;
		if (workspace.isSPMirrorEnabled()) {
			brushR = 1.0f; brushG = 0.34f; brushB = 0.02f;
		}
		else if (workspace.getVolumeInjectionMode() == VOLUME_CUT) {
			brushR = 0.05f; brushG = 0.28f; brushB = 1.0f;
		}
		const float alpha = sharedOverlap
			? (workspace.isEditingInjectionVoxel1() ? 0.92f : 0.30f) : 0.78f;
		kernelOverlayLauncher(output, m_dBrushVolume, viewportW, viewportH,
			m_volumeSize, renderMethod, zs, thetaRad, phiRad, threshold,
			sliceDistance, brushR, brushG, brushB, alpha);
		if (workspace.isSPMirrorEnabled() && m_dMirrorBrushVolume)
			kernelOverlayLauncher(output, m_dMirrorBrushVolume,
				viewportW, viewportH, m_volumeSize, renderMethod, zs,
				thetaRad, phiRad, threshold, sliceDistance,
				0.05f, 0.88f, 1.0f, alpha);
	}
	unmapGLBufferObject(*m_cudaPboResourceSlot);
	return true;
}

bool SingleParticleWorkspace::commitSPWorkingVolume(
	const SingleParticleWorkspace& workspace) {
	if (!m_dBaseVolume || !m_dWorkingVolume) return false;
	if (workspace.hasInjectionVoxelSelected())
		return commitSPInjectionBoolean(workspace);
	const bool baseSelected = workspace.getVolumePrimitiveSelection() ==
		VOLUME_PRIMITIVE_BASE;
	if (baseSelected && !m_hasCommittedGeometry) return false;
	updateSPVolumePreview(workspace);
	if (!updateSPVolumeBoundarySensor(0.0f, 0.0f) ||
		!isSPVolumeBoundarySafe()) return false;
	cudaMemcpy(m_dBaseVolume, m_dWorkingVolume, getVolumeBytes(),
		cudaMemcpyDeviceToDevice);
	threadSync();
	m_committedVolumeReady = true;
	m_hasCommittedGeometry = true;
	m_volumeDirty = false;
	return true;
}

bool SingleParticleWorkspace::commitSPInjectionBoolean(
	const SingleParticleWorkspace& workspace) {
	if (!m_dBaseVolume || !m_dWorkingVolume || !m_dBrushVolume ||
		(workspace.isSPMirrorEnabled() && !m_dMirrorBrushVolume) ||
		!workspace.hasInjectionVoxelSelected() || !m_hasCommittedGeometry)
		return false;
	generateSPVolume1BrushField(workspace, m_dBrushVolume);
	if (workspace.isSPMirrorEnabled())
		generateSPVolume1MirroredBrushField(workspace, m_dMirrorBrushVolume);
	threadSync();
	if (workspace.getVolumeInjectionMode() == VOLUME_CUT)
		markSPVolumeBoundarySafe();
	else {
		if (!classifySPVolumeBoundaryForSource(
			m_dBrushVolume, 0.0f, 0.0f) || !isSPVolumeBoundarySafe())
			return false;
		if (workspace.isSPMirrorEnabled()) {
			bool ready = false;
			unsigned int unsafe = 0;
			unsigned int inside = 0;
			if (!classifySPVolumeBoundaryForSource(m_dMirrorBrushVolume,
				0.0f, 0.0f, ready, unsafe, &inside, nullptr) ||
				!ready || unsafe != 0 || inside == 0) {
				m_volumeBoundarySensorReady = ready;
				m_volumeBoundaryUnsafeCount += unsafe;
				return false;
			}
		}
	}
	const int op = workspace.getVolumeInjectionMode() == VOLUME_CUT ? 1 : 0;
	if (workspace.isSPMirrorEnabled()) {
		composeVolumeFieldsLauncher(m_dBrushVolume, m_dMirrorBrushVolume,
			m_dMirrorBrushVolume, m_volumeSize, 0);
		composeVolumeFieldsLauncher(m_dBaseVolume, m_dMirrorBrushVolume,
			m_dWorkingVolume, m_volumeSize, op);
	}
	else {
		composeVolumeFieldsLauncher(m_dBaseVolume, m_dBrushVolume,
			m_dWorkingVolume, m_volumeSize, op);
	}
	threadSync();
	cudaMemcpy(m_dBaseVolume, m_dWorkingVolume, getVolumeBytes(),
		cudaMemcpyDeviceToDevice);
	threadSync();
	clearVolumeKernelLauncher(m_dBrushVolume, m_volumeSize, 1.0e6f);
	if (m_dMirrorBrushVolume)
		clearVolumeKernelLauncher(m_dMirrorBrushVolume, m_volumeSize, 1.0e6f);
	threadSync();
	m_committedVolumeReady = true;
	m_hasCommittedGeometry = true;
	m_volumeDirty = false;
	return true;
}
