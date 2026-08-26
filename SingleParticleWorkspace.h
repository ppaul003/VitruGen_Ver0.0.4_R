#ifndef VITRUGEN_SINGLE_PARTICLE_WORKSPACE_H
#define VITRUGEN_SINGLE_PARTICLE_WORKSPACE_H

#include <GL/glew.h>
#include <memory>
#include <vector>
#include <vector_types.h>

#include "IWorkspace.h"

class ParticleSystem;
class MarchingCubes;
class EuclidRenderer;
struct cudaGraphicsResource;

class SingleParticleWorkspace final : public IWorkspace {
public:
	enum class HostRequest { None = 0, LoadStaticParticle, SaveStaticParticle,
		SaveStaticParticleAs, ExportObj };
	enum class Layer1Item {
		Workspace = 0,
		ParticleType,
		Configure,
		Count
	};

	enum class Grid3DWorkspace {
		Graph3D = 0,
		SingleParticle,
		LinkedParticles,
		Count
	};

	enum class ObjectType {
		Static = 0,
		Composite,
		Atomic,
		Count
	};

	enum class ParticleColor {
		Red = 0,
		Blue,
		Green,
		Count
	};

	enum class ParticleRenderMode {
		Default = 0,
		Mesh,
		Count
	};

	enum class Layer2Item {
		Color = 0,
		Radius,
		RenderMode,
		Run,
		Count
	};

	enum class SubLayer {
		Reference = 0,
		ShapeEdit,
		VolumeRender,
		MarchingCubes,
		Count
	};

	enum class CollisionShape {
		Sphere = 0,
		Block,
		Capsule,
		Cone,
		DeformableSphere,
		Count
	};

	enum class MeshBoundMode {
		Default = 0,
		Fill,
		Count
	};

	enum class DisplayMode {
		Render = 0,
		RenderAndCollision,
		Wireframe,
		Count
	};

	enum class AssemblyNode {
		Preview = 0,
		EditObject,
		OffsetObject,
		ApplyToBase,
		Count
	};

	enum class VolumePrimitive {
		Base = 0,
		Sphere,
		Torus,
		Block,
		Cylinder,
		Cone,
		Capsule,
		Wedge,
		DeltaWing,
		Frustum,
		Count
	};

	enum class InjectionVoxel {
		None = 0,
		Voxel211,
		Voxel121,
		Voxel011,
		Voxel112,
		Voxel101,
		Voxel110,
		Voxel120,
		Voxel221,
		Voxel122,
		Voxel021,
		Voxel201,
		Voxel100,
		Voxel001,
		Voxel102,
		Voxel010,
		Voxel210,
		Voxel012,
		Voxel212,
		Voxel202,
		Voxel020,
		Voxel220,
		Voxel002,
		Voxel200,
		Voxel022,
		Voxel222,
		Voxel000,
		Count
	};

	enum class InjectionMode {
		Fuse = 0,
		Cut,
		Count
	};

	enum class EditTarget {
		Volume0 = 0,
		Volume1,
		Count
	};

	enum class MirrorMode {
		None = 0,
		On,
		Count
	};

	enum class OffsetVector {
		X = 0,
		Y,
		Z,
		Count
	};

	enum class ObjectEditMode {
		ScaleWhole = 0,
		ScaleZ,
		ScaleY,
		ScaleX
	};

	enum class ObjectRotationMode {
		Pitch = 0,
		Yaw,
		Roll
	};

	enum class ObjectTransformMode {
		Scale = 0,
		Rotation
	};

	enum class OverlapPreviewStatus {
		PositionInNode2 = 0,
		OutsideCage,
		Active
	};

	enum class AuthoringSource {
		ProceduralVolume = 0,
		LoadedStaticMesh
	};

	struct BasisVector {
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
	};

	struct ObjectBasis {
		BasisVector xAxis{ 1.0f, 0.0f, 0.0f };
		BasisVector yAxis{ 0.0f, 1.0f, 0.0f };
		BasisVector zAxis{ 0.0f, 0.0f, 1.0f };
	};

	struct VolumeObjectState {
		VolumePrimitive primitive = VolumePrimitive::Sphere;
		VolumePrimitive brushBasePrimitive = VolumePrimitive::Sphere;
		bool brushBaseReady = false;
		ObjectBasis basis;
		float scaleWhole = 1.0f;
		float scaleX = 1.0f;
		float scaleY = 1.0f;
		float scaleZ = 1.0f;
		float pitchDeg = 0.0f;
		float yawDeg = 0.0f;
		float rollDeg = 0.0f;
		float offsetX = 0.0f;
		float offsetY = 0.0f;
		float offsetZ = 0.0f;
	};

	// GOLD-compatible vocabulary kept local to the cartridge so the
	// transplanted CUDA/render orchestration stays algorithmically identical.
	using VolumeAssemblyNode = AssemblyNode;
	using VolumeInjectionMode = InjectionMode;
	using VolumeEditTarget = EditTarget;
	using SPMirrorMode = MirrorMode;
	using SPDisplayMode = DisplayMode;
	using SPMeshBoundMode = MeshBoundMode;

	static constexpr VolumePrimitive VOLUME_PRIMITIVE_BASE = VolumePrimitive::Base;
	static constexpr VolumePrimitive VOLUME_PRIMITIVE_SPHERE = VolumePrimitive::Sphere;
	static constexpr VolumePrimitive VOLUME_PRIMITIVE_TORUS = VolumePrimitive::Torus;
	static constexpr VolumePrimitive VOLUME_PRIMITIVE_BLOCK = VolumePrimitive::Block;
	static constexpr VolumePrimitive VOLUME_PRIMITIVE_CYLINDER = VolumePrimitive::Cylinder;
	static constexpr VolumePrimitive VOLUME_PRIMITIVE_CONE = VolumePrimitive::Cone;
	static constexpr VolumePrimitive VOLUME_PRIMITIVE_CAPSULE = VolumePrimitive::Capsule;
	static constexpr VolumePrimitive VOLUME_PRIMITIVE_WEDGE = VolumePrimitive::Wedge;
	static constexpr VolumePrimitive VOLUME_PRIMITIVE_DELTA_WING = VolumePrimitive::DeltaWing;
	static constexpr VolumePrimitive VOLUME_PRIMITIVE_FRUSTUM = VolumePrimitive::Frustum;
	static constexpr AssemblyNode VOLUME_NODE_PREVIEW = AssemblyNode::Preview;
	static constexpr AssemblyNode VOLUME_NODE_EDIT_OBJECT = AssemblyNode::EditObject;
	static constexpr AssemblyNode VOLUME_NODE_OFFSET_OBJECT = AssemblyNode::OffsetObject;
	static constexpr AssemblyNode VOLUME_NODE_APPLY_TO_BASE = AssemblyNode::ApplyToBase;
	static constexpr InjectionMode VOLUME_FUSE = InjectionMode::Fuse;
	static constexpr InjectionMode VOLUME_CUT = InjectionMode::Cut;
	static constexpr EditTarget VOLUME_EDIT_TARGET_VOXEL_0 = EditTarget::Volume0;
	static constexpr EditTarget VOLUME_EDIT_TARGET_VOXEL_1 = EditTarget::Volume1;
	static constexpr MirrorMode SP_MIRROR_NONE = MirrorMode::None;
	static constexpr MirrorMode SP_MIRROR_ON = MirrorMode::On;
	static constexpr ObjectEditMode EDIT_SCALE_WHOLE = ObjectEditMode::ScaleWhole;
	static constexpr ObjectEditMode EDIT_SCALE_Z = ObjectEditMode::ScaleZ;
	static constexpr ObjectEditMode EDIT_SCALE_Y = ObjectEditMode::ScaleY;
	static constexpr ObjectEditMode EDIT_SCALE_X = ObjectEditMode::ScaleX;
	static constexpr ObjectRotationMode ROTATE_PITCH = ObjectRotationMode::Pitch;
	static constexpr ObjectRotationMode ROTATE_YAW = ObjectRotationMode::Yaw;
	static constexpr ObjectRotationMode ROTATE_ROLL = ObjectRotationMode::Roll;
	static constexpr ObjectTransformMode TRANSFORM_SCALE = ObjectTransformMode::Scale;
	static constexpr ObjectTransformMode TRANSFORM_ROTATION = ObjectTransformMode::Rotation;
	static constexpr OffsetVector OFFSET_VECTOR_X = OffsetVector::X;
	static constexpr OffsetVector OFFSET_VECTOR_Y = OffsetVector::Y;
	static constexpr OffsetVector OFFSET_VECTOR_Z = OffsetVector::Z;
	static constexpr int PREVIEW_LIST_INJECTION_MODE = 0;
	static constexpr int INJECTION_EDIT_LIST_TARGET = 0;
	static constexpr int INJECTION_OFFSET_LIST_TARGET = 0;
	static constexpr int INJECTION_OFFSET_LIST_RAIL = 3;

	SingleParticleWorkspace();
	~SingleParticleWorkspace() override;

	bool initialize(WorkspaceServices& services) override;
	void enter(WorkspaceServices& services) override;
	void exit(WorkspaceServices& services) override;

	void update(const WorkspaceFrameContext& frame, WorkspaceServices& services) override;
	void render(const WorkspaceFrameContext& frame, WorkspaceServices& services) override;
	bool handleInput(const WorkspaceInputEvent& input, WorkspaceServices& services) override;

	WorkspacePresentation buildPresentation() const override;
	WorkspaceMenuPresentation buildMenu() const override;

	bool handleMenuCommand(int command, WorkspaceServices& services) override;

	bool initialized() const { return m_initialized; }
	bool entered() const { return m_entered; }
	void shutdown();

	HostRequest takeHostRequest() {
		const HostRequest request = m_pendingHostRequest;
		m_pendingHostRequest = HostRequest::None;
		return request;
	}

	// Persistence compatibility surface used by the host bridge.
	const int3& getVolumeSize() const { return m_volumeSize; }
	std::size_t getVolumeBytes() const;

	float* getVolume() const { return m_dWorkingVolume; }
	bool exportWorkingVolumeToHost(std::vector<float>& output) const;

	bool restoreCommittedVolumeFromHost(
		const std::vector<float>& input,
		const int3& sourceSize);

	MarchingCubes* marchingCubes() const { return m_marchingCubes.get(); }
	bool prepareMarchingCubesExport();
	void activateExportedMeshRender();
	bool hasCommittedGeometry() const { return m_hasCommittedGeometry; }
	float particleRadius() const { return m_particleRadius; }
	void activateLoadedStaticParticleBase(bool editableVolumeRestored);

	// State queries consumed by the migrated GOLD render pipeline.
	const VolumeObjectState& getVolume0State() const { return m_volume0State; }
	const VolumeObjectState& getVolume1State() const { return m_volume1State; }
	const VolumeObjectState& getActiveVolumeState() const;

	VolumePrimitive getVolumePrimitiveSelection() const { return getActiveVolumeState().primitive; }
	VolumePrimitive getResolvedVolumePrimitiveSelection() const;

	AssemblyNode getVolumeAssemblyNode() const { return m_assemblyNode; }
	InjectionMode getVolumeInjectionMode() const { return m_injectionMode; }

	ObjectEditMode getObjectEditMode() const { return m_objectEditMode; }
	ObjectRotationMode getObjectRotationMode() const { return m_objectRotationMode; }
	ObjectTransformMode getObjectTransformMode() const { return m_objectTransformMode; }

	OffsetVector getOffsetVectorSelection() const { return m_offsetVector; }

	float getOffsetIncrement() const { return m_offsetIncrement; }
	float getInjectionRailT() const { return m_injectionRailT; }

	float getOffsetX() const { return getActiveVolumeState().offsetX; }
	float getOffsetY() const { return getActiveVolumeState().offsetY; }
	float getOffsetZ() const { return getActiveVolumeState().offsetZ; }

	float getRotationPitchDeg() const { return getActiveVolumeState().pitchDeg; }
	float getRotationYawDeg() const { return getActiveVolumeState().yawDeg; }
	float getRotationRollDeg() const { return getActiveVolumeState().rollDeg; }

	float getEffectiveVolumeScaleX() const;
	float getEffectiveVolumeScaleY() const;
	float getEffectiveVolumeScaleZ() const;

	const ObjectBasis& getObjectBasis() const { return getActiveVolumeState().basis; }
	ObjectBasis getEffectiveObjectBasis() const;

	bool hasEditableVolumePrimitive() const { return getVolumePrimitiveSelection() != VOLUME_PRIMITIVE_BASE; }
	bool hasInjectionVoxelSelected() const { return m_injectionVoxel != InjectionVoxel::None; }
	bool isEditingInjectionVoxel0() const { return m_editTarget == EditTarget::Volume0; }
	bool isEditingInjectionVoxel1() const { return m_editTarget == EditTarget::Volume1; }
	bool isInjectionBrushBaseSelected() const;

	bool isSPMirrorEnabled() const { return hasInjectionVoxelSelected() && m_mirrorMode == MirrorMode::On; }
	bool isSubLayerPanelOpen() const { return m_subLayerPanelOpen; }
	bool isVolumeRenderSubLayer() const { return m_subLayer == SubLayer::VolumeRender; }
	int getActiveSubLayerPanelItem() const { return m_activePanelItem; }

	int getInjectionVoxelDX() const;
	int getInjectionVoxelDY() const;
	int getInjectionVoxelDZ() const;

	void getMirroredInjectionDirection(int& dx, int& dy, int& dz) const;

	BasisVector rotateLocalVectorXYZ(
		const BasisVector& vector,
		float pitchDeg,
		float yawDeg,
		float rollDeg) const;

	BasisVector transformByBasis(
		const ObjectBasis& basis,
		const BasisVector& localVector) const;

	ObjectBasis orthonormalizeBasis(const ObjectBasis& basis) const;
	WorkspacePresentation buildLayer1TransitionPresentation() const;

	WorkspaceRuntimeStatus buildRuntimeStatus() const;

private:
	bool handleLayer1Input(const WorkspaceInputEvent& input, WorkspaceServices& services);
	bool handleLayer2Input(const WorkspaceInputEvent& input, WorkspaceServices& services);
	bool handleLayer3Input(const WorkspaceInputEvent& input, WorkspaceServices& services);

	const char* workspaceName() const;
	const char* objectTypeName() const;
	const char* particleColorName() const;
	const char* particleRenderModeName() const;
	const char* subLayerName() const;
	const char* collisionShapeName() const;
	const char* volumePrimitiveName() const;
	const char* injectionVoxelName() const;
	const char* runtimeSubLayerName() const;
	const char* objectTransformModeName() const;
	const char* objectEditModeName() const;
	const char* objectRotationModeName() const;
	const char* offsetVectorName() const;
	const char* editTargetName() const;
	const char* overlapStatusName(OverlapPreviewStatus status) const;

	std::string buildRuntimeObjectLine() const;
	std::string buildRuntimeHelpLine() const;

	int rotationIncrementDegrees() const;

	void moveLayer1Cursor(int direction);
	void adjustLayer1Value(int direction);
	void activateLayer1(WorkspaceServices& services);
	void moveLayer2Cursor(int direction);
	void adjustLayer2Value(int direction);
	void activateLayer2(WorkspaceServices& services);
	void backOneLayer(WorkspaceServices& services);

	void placeAnchor();
	void syncParticleRendering(WorkspaceServices& services);
	void updateCameraIntent(WorkspaceServices& services) const;
	void configureWorkspaceGrid(WorkspaceServices& services) const;

	void resetRuntimeTraversal();
	void handleReferencePrimaryAction();
	void toggleSubLayerPanel();
	int activePanelItemCount() const;
	void adjustSubLayerPanelItem(int direction, WorkspaceServices& services);
	void activateSubLayerPanelItem(WorkspaceServices& services);
	bool handleVolumeDirectInput(const WorkspaceInputEvent& input);
	void enterMarchingCubes(WorkspaceServices& services);
	bool trySelectParticle(int x, int y);
	void updateHover(int x, int y);
	
	OverlapPreviewStatus runtimeOverlapStatus() const;

	WorkspacePresentation buildLayer1Presentation() const;
	WorkspacePresentation buildLayer2Presentation() const;
	WorkspacePresentation buildLayer3Presentation() const;

	void appendReferencePanel(WorkspacePresentation& presentation) const;
	void appendShapePanel(WorkspacePresentation& presentation) const;
	void appendVolumePanel(WorkspacePresentation& presentation) const;

	bool initializeCadResources();
	void releaseCadResources();
	bool initializePixelBuffer(int width, int height);
	void releasePixelBuffer();
	bool extractMarchingCubesMesh();
	void markVolumeDirty() { m_volumeDirty = true; m_volumeBoundarySensorReady = false; }
	void regenerateSPVolumeField(const SingleParticleWorkspace& workspace);
	bool commitSPWorkingVolume(const SingleParticleWorkspace& workspace);
	bool commitSPInjectionBoolean(const SingleParticleWorkspace& workspace);
	void generateSPVolume0Field(const SingleParticleWorkspace& workspace, float* destination);
	void generateSPVolume1BrushField(const SingleParticleWorkspace& workspace, float* destination);
	void generateSPVolume1MirroredBrushField(const SingleParticleWorkspace& workspace, float* destination);
	void updateSPVolumePreview(const SingleParticleWorkspace& workspace);
	void copyCommittedVolumeToPreview();
	void clearSPCommittedVolume();

	bool renderSPVolumeToPBO(
		const SingleParticleWorkspace& workspace,
		int renderMethod,
		int viewportW,
		int viewportH,
		float thetaRad,
		float phiRad,
		float zs,
		float threshold,
		float sliceDistance);

	struct SPVolumeBasis {
		float3 xAxis;
		float3 yAxis;
		float3 zAxis;
	};

	SPVolumeBasis buildSPVolumeBasis(const SingleParticleWorkspace& workspace) const;

	SPVolumeBasis buildSPVolumeBasisFromState(
		const SingleParticleWorkspace& workspace,
		const VolumeObjectState& state) const;

	void renderSPVolumeOrientationAxes(
		const SingleParticleWorkspace& workspace,
		float thetaRad,
		float phiRad);

	void renderSPVolumeInjectionVoxelPreview(
		const SingleParticleWorkspace& workspace,
		float thetaRad,
		float phiRad,
		float zs);

	void renderSPVolumeInjectionEditTargetPreview(
		const SingleParticleWorkspace& workspace,
		float thetaRad,
		float phiRad,
		float zs);

	void renderSPVolumeOffsetGrid(
		const SingleParticleWorkspace& workspace,
		float thetaRad,
		float phiRad,
		float zs);

	void renderSPVolumeTexture();
	bool initializeSPVolumeBoundarySensor();
	bool updateSPVolumeBoundarySensor(float isoValue = 0.0f, float safetyBand = 0.0f);

	bool classifySPVolumeBoundaryForSource(
		const float* source,
		float isoValue,
		float safetyBand);

	bool classifySPVolumeBoundaryForSource(
		const float* source,
		float isoValue,
		float safetyBand,
		bool& sensorReady,
		unsigned int& unsafeCount,
		unsigned int* insideSampleCount,
		std::vector<unsigned char>* boundaryMaskCPU);

	void updateSPOverlapPreviewStatus(const SingleParticleWorkspace& workspace);
	void clearSPOverlapPreviewStatus();
	void releaseSPVolumeBoundarySensor();
	void markSPVolumeBoundarySafe();

	bool isSPVolumeBoundarySafe() const { return m_volumeBoundarySensorReady && m_volumeBoundaryUnsafeCount == 0; }

	bool isSPOverlapPreviewActive() const {
		return m_spOverlapPreviewSensorReady &&
			m_spOverlapPreviewUnsafeCount == 0 &&
			m_spOverlapPreviewInsideSampleCount > 0 &&
			(!m_spOverlapPreviewMirrorRequired ||
				(m_spMirrorOverlapPreviewSensorReady &&
				m_spMirrorOverlapPreviewUnsafeCount == 0 &&
				m_spMirrorOverlapPreviewInsideSampleCount > 0));
	}

	int getSPVolumePrimitiveId(const SingleParticleWorkspace& workspace) const;
	int getSPVolumePrimitiveIdFromState(const VolumeObjectState& state) const;

	float3 buildSPVolumeOffset(const SingleParticleWorkspace& workspace) const;
	float3 buildSPVolumeOffsetFromState(const VolumeObjectState& state) const;
	float3 buildSPVolumeRailBrushOffset(const SingleParticleWorkspace& workspace) const;
	float4 buildSPVolumePrimitiveParams(const SingleParticleWorkspace& workspace) const;
	float4 buildSPVolumePrimitiveParamsFromState(const VolumeObjectState& state) const;

	VolumeObjectState& activeVolumeState();
	const VolumeObjectState& activeVolumeState() const;

	void resetVolumeState(VolumeObjectState& state, VolumePrimitive primitive = VolumePrimitive::Sphere);
	void cycleVolumePrimitiveSelection(int direction);
	void cycleInjectionVoxelSelection(int direction);
	void cycleVolumeEditTarget(int direction);
	void cycleVolumeInjectionMode(int direction);
	void cycleMirrorMode(int direction);
	void cycleOffsetVector(int direction);
	void cycleOffsetIncrement(int direction);
	void cycleRotationIncrement(int direction);
	void adjustInjectionRail(int direction);
	void adjustObjectOffset(int direction);
	void adjustObjectScale(int direction);
	void adjustObjectRotation(int direction);
	void resetObjectOffset();
	void resetObjectScale();
	void resetObjectRotation();
	void commitObjectRotationToBasis();
	void commitBrushBase();
	bool canApplyVolumeToBase() const;
	void finalizeCommittedBase();
	void returnFromMarchingCubesToPreview();
	void returnFromMarchingCubesToReference();

private:
	static constexpr float kParticleWorldBoundary = 1.0f;
	static constexpr float kParticleGridDim = 64.0f;
	static constexpr float kParticleCellSize =
		(2.0f * kParticleWorldBoundary) / kParticleGridDim;
	static constexpr float kParticleRadiusMax = 0.5f * kParticleCellSize;
	static constexpr float kParticleRadiusMin = 0.25f * kParticleRadiusMax;
	static constexpr float kParticleRadiusDefault =
		0.5f * (kParticleRadiusMin + kParticleRadiusMax);
	static constexpr float kParticleRadiusStep =
		(kParticleRadiusMax - kParticleRadiusMin) / 16.0f;

	bool m_initialized = false;
	bool m_entered = false;
	WorkspaceServices* m_services = nullptr;
	WorkspaceFrameContext m_lastFrame;

	std::unique_ptr<ParticleSystem> m_particleSystem;
	ParticleSystem* m_singleParticleSystem = nullptr;
	std::vector<float> m_particleRadii;
	std::vector<float>* m_singleParticleRadii = nullptr;
	EuclidRenderer* m_renderer = nullptr;
	bool m_anchorPlaced = false;

	Layer1Item m_layer1Item = Layer1Item::Workspace;
	Grid3DWorkspace m_grid3DWorkspace = Grid3DWorkspace::SingleParticle;
	ObjectType m_objectType = ObjectType::Static;

	Layer2Item m_layer2Item = Layer2Item::Color;
	ParticleColor m_particleColor = ParticleColor::Red;
	ParticleRenderMode m_particleRenderMode = ParticleRenderMode::Default;
	float m_particleRadius = kParticleRadiusDefault;

	SubLayer m_subLayer = SubLayer::Reference;
	CollisionShape m_collisionShape = CollisionShape::Sphere;
	MeshBoundMode m_meshBoundMode = MeshBoundMode::Default;
	DisplayMode m_displayMode = DisplayMode::Render;
	bool m_renderCageVisible = true;

	bool m_selectionArmed = false;
	bool m_selectedParticle = false;
	bool m_hoverValid = false;
	float m_hoverX = 0.0f;
	float m_hoverY = 0.0f;
	int m_workplaneSlice = 0;

	bool m_subLayerPanelOpen = false;
	int m_activePanelItem = 0;

	AssemblyNode m_assemblyNode = AssemblyNode::Preview;
	VolumeObjectState m_volume0State;
	VolumeObjectState m_volume1State;
	EditTarget m_editTarget = EditTarget::Volume0;
	InjectionVoxel m_injectionVoxel = InjectionVoxel::None;
	InjectionMode m_injectionMode = InjectionMode::Fuse;
	MirrorMode m_mirrorMode = MirrorMode::None;
	OffsetVector m_offsetVector = OffsetVector::X;
	ObjectEditMode m_objectEditMode = ObjectEditMode::ScaleWhole;
	ObjectRotationMode m_objectRotationMode = ObjectRotationMode::Pitch;
	ObjectTransformMode m_objectTransformMode = ObjectTransformMode::Scale;
	int m_rotationIncrementIndex = 0;
	int m_offsetIncrementIndex = 0;
	float m_offsetIncrement = 0.01f;
	float m_injectionRailT = 0.0f;

	std::unique_ptr<MarchingCubes> m_marchingCubes;
	bool m_volumeDirty = true;
	bool m_committedVolumeReady = false;
	bool m_hasCommittedGeometry = false;
	float* m_dWorkingVolume = nullptr;
	float* m_dBaseVolume = nullptr;
	float* m_dBrushVolume = nullptr;
	float* m_dMirrorBrushVolume = nullptr;
	int3 m_volumeSize{ 128, 128, 128 };
	GLuint m_pbo = 0;
	GLuint m_tex = 0;
	int m_pboWidth = 0;
	int m_pboHeight = 0;
	cudaGraphicsResource* m_cudaPboResource = nullptr;
	cudaGraphicsResource** m_cudaPboResourceSlot = &m_cudaPboResource;
	unsigned char* m_dVolumeBoundaryMask = nullptr;
	unsigned int* m_dVolumeBoundaryUnsafeCount = nullptr;
	unsigned int* m_dVolumeInsideSampleCount = nullptr;
	std::vector<unsigned char> m_volumeBoundaryMaskCPU;
	unsigned int m_volumeBoundaryFaceStride = 0;
	unsigned int m_volumeBoundaryUnsafeCount = 0;
	bool m_volumeBoundarySensorReady = false;
	unsigned int m_spOverlapPreviewUnsafeCount = 0;
	unsigned int m_spOverlapPreviewInsideSampleCount = 0;
	bool m_spOverlapPreviewSensorReady = false;
	unsigned int m_spMirrorOverlapPreviewUnsafeCount = 0;
	unsigned int m_spMirrorOverlapPreviewInsideSampleCount = 0;
	bool m_spMirrorOverlapPreviewSensorReady = false;
	bool m_spOverlapPreviewMirrorRequired = false;
	OverlapPreviewStatus m_overlapPreviewStatus = OverlapPreviewStatus::PositionInNode2;
	AuthoringSource m_authoringSource = AuthoringSource::ProceduralVolume;
	bool m_loadedStaticMeshHasEditableVolume = false;
	HostRequest m_pendingHostRequest = HostRequest::None;
};

#endif
