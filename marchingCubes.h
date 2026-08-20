#ifndef MARCHING_CUBES_H
#define MARCHING_CUBES_H

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <GL/glew.h>
#include <GL/freeglut.h>
#include <cuda_gl_interop.h>

#include <cstdio>
#include <algorithm>

#include <vector>
#include <cuda_runtime.h>
#include <vector_types.h>

#include "kernel.h"
#include "MeshGeometry.h"
#include "MeshProcessor.h"
#include "params_kernel.cuh"

class MarchingCubes {
public:
	MarchingCubes();
	~MarchingCubes();
	bool init(int3 volumeSize);

	bool isInitialized() const { return m_initialized; }
	bool hasTriangleData() const { return m_triangleDataValid; }
	bool hasMeshBounds() const { return m_meshBoundsValid; }
	bool exportOBJ(const char* filename, bool writeNormals = true);

	void shutdown();
	void classifyOnly(float* dVolume, float isoValue);
	void generateTriangles(float* dVolume, float isoValue);
	void updateActiveVoxelDebugBuffer();
	void extract(float* dVolume, float isoValue);
	void createMeshVBOs();
	void destroyMeshVBos();
	void renderWireframe(float thetaRad, float phiRad, float zs);

	const std::vector<uint>& getActiveVoxelIdsCPU() const { return m_activeVoxelIdsCPU; }
	const std::vector<float4>& getTriangleVertsCPU() const { return m_triangleVertsCPU; }
	const std::vector<float4>& getTriangleNormsCPU() const { return m_triangleNormsCPU; }
	const vitru::MeshGeometry& getCanonicalMesh() const { return m_canonicalMesh; }
	const vitru::MeshProcessingReport& getMeshProcessingReport() const { return m_meshProcessingReport; }

	uint3 getGridSize() const { return m_gridSize; }

	float3 getMeshMin() const { return m_meshMin; }
	float3 getMeshMax() const { return m_meshMax; }
	float3 getMeshCenter() const { return m_meshCenter; }

	uint getActiveVoxelCount() const { return m_activeVoxels; }
	uint getTotalVertexCount() const { return m_totalVerts; }
	uint getNumVoxels() const { return m_numVoxels; }
	uint getMaxVerts() const { return m_maxVerts; }
	uint getAllocatedTriangleVertexCount() const { return m_allocatedTriangleVerts; }
	uint getGeneratedTriangleCount() const { return m_totalVerts / 3u; }
	uint getAllocatedMeshVertexCount() const { return m_allocatedMeshVerts; }

	size_t getMeshVBOBytesEach() const { return static_cast<size_t>(m_allocatedMeshVerts) * sizeof(float4); }

	uint* getCompactedVoxelArray() const { return m_dCompVoxelArray; }
	uint* getVoxelVertsScan() const { return m_dVoxelVertsScan; }

	float4* getTriangleVertsDevice() const { return m_dTriangleVerts; }
	float4* getTriangleNormsDevice() const { return m_dTriangleNorms; }

private:
	static bool isPowerOfTwo(int v) {
		return v > 0 && ((v & (v - 1)) == 0);
	}
	static uint log2u(uint v) {
		uint r = 0;
		while (v > 1u) {
			v >>= 1u;
			r++;
		}
		return r;
	}
	static uint iDivUp(uint a, uint b) {
		return (a + b - 1u) / b;
	}

	bool ensureTriangleBuffers(uint requiredVerts) {
		if (requiredVerts == 0) {
			m_triangleDataValid = false;
			return false;
		}

		if (m_dTriangleVerts &&
			m_dTriangleNorms &&
			m_allocatedTriangleVerts >= requiredVerts) {
			return true;
		}

		freeTriangleBuffers();

		const size_t bytes =
			static_cast<size_t>(requiredVerts) * sizeof(float4);

		allocateArray(
			reinterpret_cast<void**>(&m_dTriangleVerts),
			bytes
		);

		allocateArray(
			reinterpret_cast<void**>(&m_dTriangleNorms),
			bytes
		);

		m_allocatedTriangleVerts = requiredVerts;

		printf(
			"[MarchingCubes3D] Allocated triangle buffers: verts=%u, bytes=%zu each\n",
			requiredVerts,
			bytes
		);

		return true;
	}

	void freeTriangleBuffers() {
		if (m_dTriangleVerts) {
			freeArray(m_dTriangleVerts);
			m_dTriangleVerts = nullptr;
		}

		if (m_dTriangleNorms) {
			freeArray(m_dTriangleNorms);
			m_dTriangleNorms = nullptr;
		}

		m_allocatedTriangleVerts = 0;
		m_triangleDataValid = false;
	}
	void debugPrintFirstTriangle() {
		if (!m_dTriangleVerts) return;
		if (m_totalVerts < 3) return;

		float4 h[3];

		copyArrayFromDevice(
			h,
			m_dTriangleVerts,
			nullptr,
			static_cast<int>(sizeof(h))
		);

		printf(
			"[MarchingCubes3D] first triangle:\n"
			"  v0 = %.4f %.4f %.4f\n"
			"  v1 = %.4f %.4f %.4f\n"
			"  v2 = %.4f %.4f %.4f\n",
			h[0].x, h[0].y, h[0].z,
			h[1].x, h[1].y, h[1].z,
			h[2].x, h[2].y, h[2].z
		);
	}
	void updateTriangleDebugBuffers() {
		m_triangleVertsCPU.clear();
		m_triangleNormsCPU.clear();

		if (!m_triangleDataValid) return;
		if (!m_dTriangleVerts || !m_dTriangleNorms) return;
		if (m_totalVerts == 0) return;

		m_triangleVertsCPU.resize(m_totalVerts);
		m_triangleNormsCPU.resize(m_totalVerts);

		copyArrayFromDevice(
			m_triangleVertsCPU.data(),
			m_dTriangleVerts,
			nullptr,
			static_cast<int>(m_totalVerts * sizeof(float4))
		);

		copyArrayFromDevice(
			m_triangleNormsCPU.data(),
			m_dTriangleNorms,
			nullptr,
			static_cast<int>(m_totalVerts * sizeof(float4))
		);
	}
	void rebuildCanonicalMesh();

private:
	bool m_initialized = false;
	bool m_triangleDataValid = false;
	bool m_meshBoundsValid = false;

	int3 m_volumeSize{};
	uint3 m_gridSize{};
	uint3 m_gridSizeShift{};
	uint3 m_gridSizeMask{};

	float3 m_voxelSize{};
	float3 m_meshMin{ 0.0f, 0.0f, 0.0f };
	float3 m_meshMax{ 0.0f, 0.0f, 0.0f };
	float3 m_meshCenter{ 0.0f, 0.0f, 0.0f };

	GLuint m_posVbo = 0;
	GLuint m_normVbo = 0;

	uint m_numVoxels = 0;
	uint m_maxVerts = 0;
	uint m_activeVoxels = 0;
	uint m_totalVerts = 0;

	uint m_allocatedTriangleVerts = 0;
	uint m_allocatedMeshVerts = 0;

	uint* m_dVoxelVerts = nullptr;
	uint* m_dVoxelVertsScan = nullptr;
	uint* m_dVoxelOccupied = nullptr;
	uint* m_dVoxelOccupiedScan = nullptr;
	uint* m_dCompVoxelArray = nullptr;

	uint* m_dEdgeTable = nullptr;
	uint* m_dTriTable = nullptr;
	uint* m_dNumVertsTable = nullptr;

	float4* m_dTriangleVerts = nullptr;
	float4* m_dTriangleNorms = nullptr;

	cudaGraphicsResource* m_cudaPosVboResource = nullptr;
	cudaGraphicsResource* m_cudaNormVboResource = nullptr;

	std::vector<float4> m_triangleVertsCPU;
	std::vector<float4> m_triangleNormsCPU;
	vitru::MeshGeometry m_canonicalMesh;
	vitru::MeshProcessingReport m_meshProcessingReport;

	std::vector<uint> m_activeVoxelIdsCPU;
	uint m_debugActiveVoxelCopyLimit = 65536;
};


#endif
