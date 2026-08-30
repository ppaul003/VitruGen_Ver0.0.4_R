#ifndef __RENDER_EUCLID__
#define __RENDER_EUCLID__

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <GL/glew.h>
#include <vector>
#include <string>
#include <cstdint>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>

#include <vector_types.h>

#include "particleSystem.h"
#include "StaticParticleAsset.h"
#include "PngImage.h"

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif

class EuclidRenderer {
public:
	enum DisplayMode {
		PARTICLE_POINTS,
		PARTICLE_SPHERES,
		VOLUME_TEXTURE,
		PARTICLE_NUM_MODES
	};

	enum GridMode {
		GRID_3D = 0,
		GRID_2D
	};

	enum GridPlane {
		PLANE_XY = 0,
		PLANE_XZ,
		PLANE_YZ
	};

	enum VolumeAxisGuideMode {
		VOLUME_AXIS_GUIDE_DEFAULT = 0,

		// Scale-edit guides.
		VOLUME_AXIS_GUIDE_X,
		VOLUME_AXIS_GUIDE_Y,
		VOLUME_AXIS_GUIDE_Z,

		// Rotation-edit guides.
		VOLUME_AXIS_GUIDE_PITCH,
		VOLUME_AXIS_GUIDE_YAW,
		VOLUME_AXIS_GUIDE_ROLL
	};

	enum VolumeOffsetAxis {
		VOLUME_OFFSET_AXIS_X = 0,
		VOLUME_OFFSET_AXIS_Y,
		VOLUME_OFFSET_AXIS_Z
	};

	struct UniformGrid {

		glm::ivec3 dimensions{
			kGridDim, 
			kGridDim, 
			kGridDim
		};

		glm::vec3 origin{
			-kSimHalfBox,
			-kSimHalfBox,
			-kSimHalfBox
		};
		
		glm::vec3 cellSize{
			kCellSize,
			kCellSize,
			kCellSize
		};

		int majorEvery = kMajorEvery;
	};

	struct GridDisplay {
		bool boundary = true;
		bool majorGrid = true;
		bool minorGrid = false;
		bool axes = true;
	};

	struct VolumeObjectBasis {
		glm::vec3 xAxis{ 1.0f, 0.0f, 0.0f };
		glm::vec3 yAxis{ 0.0f, 1.0f, 0.0f };
		glm::vec3 zAxis{ 0.0f, 0.0f, 1.0f };
	};

	struct ParticleMeshVertex {
		glm::vec3 position{ 0.0f };
		glm::vec3 normal{ 0.0f, 1.0f, 0.0f };
		glm::vec2 texcoord{ 0.0f };
	};

	struct ParticleMeshDrawRange {
		GLint firstVertex = 0;
		GLsizei vertexCount = 0;
		std::uint32_t materialIndex = 0;
	};

	struct ParticleMeshTexture {
		std::string id;
		GLuint handle = 0;
	};

public:
	EuclidRenderer();
	~EuclidRenderer();

	void setWindowSize(int w, int h) { m_window_w = w; m_window_h = h; }
	void setFOV(float fov) { m_fov = fov; }

	void setSimBoxSize(int x) { m_simBox = x; }
	int getSimBoxSize() const { return m_simBox; }

	void setGridDimSize(int x) { m_gridDimSize = x; }
	int getGridDimSize() const { return m_gridDimSize; }

	void setGridMajorEvery(int x) { m_gridMajorEvery = x; }
	int getGridMajorEvery() const { return m_gridMajorEvery; }

	void setGridStyle(int majorEvery, bool drawMinor);

	void setWorkspaceGridVisibility(
		bool drawBoundary,
		bool drawMajor,
		bool drawMinor,
		bool drawAxes
	);

	void setGridMode3D();
	void setGridMode2D(GridPlane plane, int sliceOffset);
	void setRadius(float* r, int numParticles);
	void setPointSize(float size) { m_pointSize = size; }
	void setParticleRadius(float r) { m_particleRadius = r; }
	void setParticleHighlighted(bool highlighted) { m_particleHighlighted = highlighted; }
	void setDisplayMode(DisplayMode mode) { m_displayMode = mode; }
	void setParticleSystem(ParticleSystem* psystem) { m_psystem = psystem; }
	void setPositions(float* pos, int numParticles);
	void setVertexBuffer(unsigned int vbo, int numParticles);
	void setColorBuffer(unsigned int vbo) { m_colorVBO = vbo; }
	void setRadiusBuffer(unsigned int vbo) { m_radVBO = vbo; }

	void setGrid(
		const glm::ivec3& gridDim,
		const glm::vec3& worldOrigin,
		const glm::vec3& cellSize
	);

	DisplayMode getDisplayMode() const { return m_displayMode; }

	// ----------------------------
	// GENERIC DIAGNOSTIC GEOMETRY
	// ----------------------------
	void display(DisplayMode mode = PARTICLE_POINTS);
	void displayGrid();

	void drawGridBoundary(const UniformGrid& grid);
	void drawGridAxes(const UniformGrid& grid);

	void drawGridPlane(
		const UniformGrid& grid,
		GridPlane plane,
		float planePosition,
		bool drawMinorLines = true
	);

	void drawUniformGrid(
		const UniformGrid& grid,
		const GridDisplay& display
	);

	void drawUniformGridZRange(
		const UniformGrid& grid,
		float visibleMinZ,
		float visibleMaxZ,
		const GridDisplay& display
	);

	void drawWireCube(
		const glm::vec3& center,
		const glm::vec3& halfExtent
	);

	// ----------------------------
	// SINGLE_PARTICLE WORKSPACE
	// ----------------------------
	void displayParticleWorkspace(
		float thetaRad,
		float phiRad,
		float zs,
		int workplaneSlice,
		bool showWorkplane,
		bool selected,
		bool hoverValid,
		float hoverX,
		float hoverY,
		bool useMeshRender = false,
		bool fillMeshBounds = false,
		bool wireframe = false,
		bool showCollisionProxy = false,
		bool showRenderCage = false
	);

	// Displays the currently loaded StaticParticleAsset inside the
	// SINGLE_PARTICLE volumetric-editor coordinate frame.
	//
	// This is a mesh-backed BASE preview. It does not imply that a
	// CUDA scalar field or editable SDF exists.
	void displayParticleMeshVolumeWorkspace(
		float thetaRad,
		float phiRad,
		float zs,
		int volumeDim,
		bool selected,
		bool wireframe
	);

	void displayXYWorkplane(
		int slice,
		bool hoverValid,
		float hoverX,
		float hoverY,
		bool selected
	);
	
	void displayVolumeOrientationAxes(
		float thetaRad,
		float phiRad,
		VolumeAxisGuideMode guideMode,
		const VolumeObjectBasis& bakedBasis,
		const VolumeObjectBasis& effectiveBasis,
		float pitchDeg = 0.0f,
		float yawDeg = 0.0f,
		float rollDeg = 0.0f
	);

	void displayVolumeInjectionVoxelPreview(
		float thetaRad,
		float phiRad,
		float zs,
		int volumeDim,
		float alphaScale = 0.55f,
		int injectionDx = 0,
		int injectionDy = 0,
		int injectionDz = 0
	);

	void displayVolumeInjectionEditTargetPreview(
		float thetaRad,
		float phiRad,
		float zs,
		int volumeDim,
		float alphaScale = 0.55f,
		int injectionDx = 0,
		int injectionDy = 0,
		int injectionDz = 0,
		bool editingVoxel1 = false,
		bool sharedOverlapActive = false
	);

	void displayVolumeInjectionRailMarker(
		float thetaRad,
		float phiRad,
		float zs,
		int volumeDim,
		int injectionDx,
		int injectionDy,
		int injectionDz,
		float railT,
		float alphaScale = 1.0f,
		float brushOffsetX = 0.0f,
		float brushOffsetY = 0.0f,
		float brushOffsetZ = 0.0f
	);

	void displaySPMirrorGuides(
		float thetaRad,
		float phiRad,
		float zs,
		int volumeDim,
		int injectionDx,
		int injectionDy,
		int injectionDz,
		bool editingVoxel1,
		bool sharedOverlapActive,
		float railT,
		float brushOffsetX,
		float brushOffsetY,
		float brushOffsetZ
	);

	void displayVolumeOffsetGrid(
		float thetaRad,
		float phiRad,
		float zs,
		int volumeDim,
		VolumeOffsetAxis offsetAxis,
		float normalizedIncrement,
		float normalizedOffsetX,
		float normalizedOffsetY,
		float normalizedOffsetZ,
		const std::vector<unsigned char>& boundaryMask,
		unsigned int boundaryFaceStride
	);

	void drawVolumeOffsetMarkers(
		int volumeDim,
		VolumeOffsetAxis offsetAxis,
		float normalizedOffsetX,
		float normalizedOffsetY,
		float normalizedOffsetZ
	);

	void displayMarchingCubesVoxelGrid(
		float thetaRad,
		float phiRad,
		float zs,
		int volumeDim = 128,
		int majorEvery = 16,
		float halfExtent = 1.20f
	);

	void displayActiveVoxelWireFrame(
		float thetaRad,
		float phiRad,
		float zs,
		const std::vector<uint>& activeVoxelIds,
		int gridX,
		int gridY,
		int gridZ,
		int maxBoxes = 8192,
		float alpha = 0.32f
	);

	void displayMarchingCubesTriangleWireFrame(
		float thetaRad,
		float phiRad,
		float zs,
		const std::vector<float4>& verts,
		float alpha = 1.0f,
		int maxTriangle = 250000
	);

	void clearParticleMeshOBJ();

	void attachTexture(GLuint tex) { m_tex = tex; }
	void attachPixelBuffer(GLuint pbo) { m_pbo = pbo; }
	void displayVolumeTexture();

	GLuint getTexture() const { return m_tex; }
	GLuint getPixelBuffer() const { return m_pbo; }
	GLuint getParticleMeshVBO() const { return m_particleMeshVBO; }

	GLsizei getParticleMeshVertexCount() const { return m_particleMeshVertexCount; }

	bool hasParticleMeshOBJ() const { return m_particleMeshLoaded && m_particleMeshVBO != m_particleMeshVertexCount > 0; }
	bool loadParticleMeshOBJ(const char* filename);
	bool loadParticleStaticAsset(const vitru::StaticParticleAsset& asset);
	bool particleIntersectsSlice(const ParticleProxy3D& p, int slice) const;
	float getParticleMeshMaxExtent() const { return m_particleMeshMaxExtent; }
	glm::vec3 getParticleMeshMin() const { return m_particleMeshMin; }
	glm::vec3 getParticleMeshMax() const { return m_particleMeshMax; }
	glm::vec3 getParticleMeshCenter() const { return m_particleMeshCenter; }
	size_t getParticleMeshBufferBytes() const { return static_cast<size_t>(m_particleMeshVertexCount) * sizeof(ParticleMeshVertex); }

	void drawParticleMeshOBJ(
		const ParticleProxy3D&,
		const float4& color,
		bool selected,
		bool fillMeshBounds,
		bool wireframe
	);

	void drawStaticParticleTargetPreview(
		float previewRadius,
		bool showCollisionRadius
	);

	void displayTextureMapStaticParticlePreview(
		float thetaRad,
		float phiRad,
		float zs,
		float previewRadius,
		bool showCollisionRadius,
		bool showConfigurationGuides,
		bool selectedTarget,
		float revealProgress = 1.0f
	);

	void displayTextureMapPixelEditor(
		const vitru::ImageRGBA8& atlasImage,
		std::uint32_t faceIndex,
		std::uint32_t gridDivisions,
		int cursorX,
		int cursorY,
		const std::vector<vitru::Vec2>& contourPoints,
		bool contourClosed,
		float editorZoom,
		float transitionVisibility = 1.0f
	);

	bool hitTestTextureMapPreview(int x, int y) const;

protected:
	void _initGL();
	void _initialize();
	void _drawPoints(bool useColorBuffer = true);
	
	void drawParticleWireSphere(
		const ParticleProxy3D& p,
		const float4& color,
		float lineWidth,
		float alpha,
		bool overlay
	);

	void drawParticleRenderCage(const ParticleProxy3D& p);

	GLuint _compileProgram(
		const char* vsource,
		const char* fsource,
		const char* secondaryAttributeName,
		const char* programLabel
	);

	void drawAxes();
	void drawWorkspaceBoundary();
	void drawWorkspaceMajorGrid();
	
	void drawVolumeAxes(
		VolumeAxisGuideMode guidMode,
		const VolumeObjectBasis& bakedBasis,
		const VolumeObjectBasis& effectiveBasis,
		float pitchDeg,
		float yawDeg,
		float rollDeg
	);

	void drawVolumeBoundaryCage(
		int volumeDim,
		int majorEvery,
		float alphaScale = 1.0f,
		const glm::vec4& majorColor = glm::vec4(0.25, 1.00f, 0.78f, 0.16f),
		const glm::vec4& minorColor = glm::vec4(0.10f, 0.82f, 0.66f, 0.045f),
		const glm::vec4& edgeColor = glm::vec4(0.38f, 1.00f, 0.84f, 0.72f)
	);

	void drawVolumeBoundaryContactPatches(
		int volumeDim,
		const std::vector<unsigned char>& boundaryMask,
		unsigned int boundaryFaceStride
	);

	void drawVolumeOffsetGridPlane(
		int volumeDim,
		VolumeOffsetAxis offsetAxis,
		float normalizedIncrement
	);

	void drawXYWorkplane(
		int slice,
		bool hoverValid,
		float hoverX,
		float hoverY
	);

	void drawMarchingCubesVoxelGridWire(
		int volumeDim,
		int majorEvery,
		float halfExtent
	);

	void drawActiveVoxelCellWire(
		uint voxelID,
		int gridX,
		int gridY,
		int gridZ
	);

	void drawGridPlaneLines(
		const UniformGrid& grid,
		GridPlane plane,
		float planePosition,
		bool drawMinorLines
	);

	void drawParticleSphere(
		GLuint program,
		const float pos[4],
		float radius,
		const float color[4],
		bool emissiveBlend
	);

	bool checkShader(GLuint shader, const char* label);
	bool uploadParticleMeshVBO();
	void computeParticleMeshBounds();

private:
	static constexpr float kSimBoxSize = 4.0f;
	static constexpr float kSimHalfBox = kSimBoxSize * 0.5f;
	static constexpr int kMajorEvery = 8;
	static constexpr int kGridDim = 16;
	static constexpr float kCellSize =
		kSimBoxSize / static_cast<float>(kGridDim);

	GridMode m_gridMode = GRID_3D;
	GridPlane m_workPlane = PLANE_XY;
	DisplayMode m_displayMode = PARTICLE_SPHERES;

	bool m_bInitialized;
	bool m_gridEnabled = true;
	bool m_drawBoundaryGrid = true;
	bool m_drawMajorGrid = true;
	bool m_drawMinorGrid = false;
	bool m_drawAxes = true;
	bool m_particleMeshLoaded = false;
	bool m_particleHighlighted = false;

	int m_simBox = 4;
	int m_gridDimSize= 64;
	int m_gridMajorEvery = 8;

	float m_fov = 60.0f;
	int m_window_w = 1920;
	int m_window_h = 1080;

	int m_sliceOffset = 0;

	int m_radCapacity;
	int m_numParticles;

	float m_particleMeshMaxExtent = 1.0f;
	float m_particleHighlightScale = 1.35f;

	float m_pointSize;
	float m_particleRadius;

	float* m_rad;
	float* m_pos;

	GLuint m_program0; // sphere shader
	GLuint m_program1; // sphere light
	GLuint m_meshProgram; // OBJ triangle mesh shader

	GLuint m_vbo;
	GLuint m_radVBO;
	GLuint m_colorVBO;

	GLuint m_pbo = 0;
	GLuint m_tex = 0;

	GLuint m_particlePosVBO = 0;
	GLuint m_particleRadVBO = 0;
	GLuint m_particleColorVBO = 0;

	glm::ivec3 m_gridDim{ 0, 0, 0 };
	glm::vec3 m_gridOrigin{ 0, 0, 0 };
	glm::vec3 m_cellSize{ 1, 1, 1 };

	glm::mat4 m_root = glm::mat4(1.0f);

	ParticleSystem* m_psystem = nullptr;

	GLint m_meshColorLocation;
	GLint m_meshLightDirLocation;
	GLint m_meshAmbientLocation;
	GLint m_meshTexcoordAttributeLocation = -1;
	GLint m_meshSamplerLocation = -1;
	GLint m_meshUseTextureLocation = -1;
	GLint m_meshAlphaMaskLocation = -1;
	GLint m_meshAlphaCutoffLocation = -1;
	GLint m_meshEmissiveSamplerLocation = -1;
	GLint m_meshUseEmissiveLocation = -1;
	GLint m_meshEmissiveFactorLocation = -1;
	GLint m_meshEmissiveIntensityLocation = -1;

	GLuint m_particleMeshVBO = 0;
	GLuint m_particleMeshWhiteTexture = 0;
	GLsizei m_particleMeshVertexCount = 0;

	std::string m_particleMeshPath;

	std::vector<glm::vec3> m_particleMeshVerts;
	std::vector<glm::vec3> m_particleMeshNorms;
	std::vector<glm::vec2> m_particleMeshUVs;
	std::vector<glm::vec2> m_textureMapPreviewScreenVertices;
	std::vector<ParticleMeshDrawRange> m_particleMeshDrawRanges;
	std::vector<vitru::MaterialSlot> m_particleMeshMaterials;
	std::vector<ParticleMeshTexture> m_particleMeshTextures;

	glm::vec3 m_particleMeshMin{ 0.0f };
	glm::vec3 m_particleMeshMax{ 0.0f };
	glm::vec3 m_particleMeshCenter{ 0.0f };
};

#endif
