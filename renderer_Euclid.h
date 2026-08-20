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
	enum GridPlane {
		PLANE_XY = 0,
		PLANE_XZ,
		PLANE_YZ
	};

	enum DisplayMode {
		PARTICLE_POINTS,
		PARTICLE_SPHERES,
		VOLUME_TEXTURE,
		PARTICLE_NUM_MODES
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

	void setGridDimSize(int x) { m_gridDim = x; }
	int getGridDimSize() const { return m_gridDim; }

	void setGridMajorEvery(int x) { m_gridMajorEvery = x; }
	int getGridMajorEvery() const { return m_gridMajorEvery; }

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

	// ----------------------------
	// GENERIC DIAGNOSTIC GEOMETRY
	// ----------------------------
	
	void drawWireCube(
		const glm::vec3& center,
		const glm::vec3& halfExtent
	);

protected:
	void _initGL();
	void _initialize();
	void _drawPoints(bool useColorBuffer = true);
	
	GLuint _compileProgram(
		const char* vsource,
		const char* fsource,
		const char* secondaryAttributeName,
		const char* programLabel
	);

	void drawGridPlaneLines(
		const UniformGrid& grid,
		GridPlane plane,
		float planePosition,
		bool drawMinorLines
	);

private:
	static constexpr float kSimBoxSize = 4.0f;
	static constexpr float kSimHalfBox = kSimBoxSize * 0.5f;
	static constexpr int kMajorEvery = 4;
	static constexpr int kGridDim = 16;
	static constexpr float kCellSize =
		kSimBoxSize / static_cast<float>(kGridDim);

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
	int m_gridDim = 16;
	int m_gridMajorEvery = 4;

	int m_window_w = 1920;
	int m_window_h = 1080;

	int m_radCapacity;
	int m_numParticles;

	float m_fov = 60.0f;

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

	glm::mat4 m_root = glm::mat4(1.0f);

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