#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>

#include "marchingCubes.h"
#include "MeshUVGenerator.h"

using namespace std;

MarchingCubes::MarchingCubes() {}
MarchingCubes::~MarchingCubes() {}

bool MarchingCubes::init(int3 volumeSize) {
	if (m_initialized) return true;

	m_volumeSize = volumeSize;

	// Current MC bit-mask indexing expects power-of-two dimensions.
	if (!isPowerOfTwo(volumeSize.x) ||
		!isPowerOfTwo(volumeSize.y) ||
		!isPowerOfTwo(volumeSize.z)) {

		printf(
			"[MarchingCubes3D] ERROR: volume dimensions must be power-of-two for current mcCalcGridPos(). Got %d x %d x %d\n",
			volumeSize.x,
			volumeSize.y,
			volumeSize.z
		);
		return false;
	}

	m_gridSize = make_uint3(
		static_cast<uint>(volumeSize.x),
		static_cast<uint>(volumeSize.y),
		static_cast<uint>(volumeSize.z)
	);

	m_gridSizeShift = make_uint3(
		0,
		log2u(m_gridSize.x),
		log2u(m_gridSize.x) + log2u(m_gridSize.y)
	);

	m_gridSizeMask = make_uint3(
		m_gridSize.x - 1u,
		m_gridSize.y - 1u,
		m_gridSize.z - 1u
	);

	m_numVoxels =
		m_gridSize.x *
		m_gridSize.y *
		m_gridSize.z;

	m_voxelSize = make_float3(1.0f, 1.0f, 1.0f);

	// Worst-case Marching Cubes output: 5 triangles = 15 vertices per cell.
	m_maxVerts = m_numVoxels * 15u;

	const size_t uintBytes =
		static_cast<size_t>(m_numVoxels) * sizeof(uint);

	allocateArray(reinterpret_cast<void**>(&m_dVoxelVerts), uintBytes);
	allocateArray(reinterpret_cast<void**>(&m_dVoxelVertsScan), uintBytes);
	allocateArray(reinterpret_cast<void**>(&m_dVoxelOccupied), uintBytes);
	allocateArray(reinterpret_cast<void**>(&m_dVoxelOccupiedScan), uintBytes);
	allocateArray(reinterpret_cast<void**>(&m_dCompVoxelArray), uintBytes);

	allocateMarchingCubesTables(
		&m_dEdgeTable,
		&m_dTriTable,
		&m_dNumVertsTable
	);

	MarchingCubesParams params{};
	params.volumeSize = m_gridSize;
	params.voxelSize = m_voxelSize;
	params.isoValue = 0.0f;
	params.numCells = m_numVoxels;
	params.maxVerts = m_maxVerts;

	setMarchingCubesParameters(&params);

	m_initialized = true;

	printf(
		"[MarchingCubes3D] Initialized: grid=%u x %u x %u, voxels=%u, maxVerts=%u\n",
		m_gridSize.x,
		m_gridSize.y,
		m_gridSize.z,
		m_numVoxels,
		m_maxVerts
	);

	return true;
}
bool MarchingCubes::exportOBJ(const char* filename, bool writeNormals) {
	if (!filename || filename[0] == '\0') {
		printf("[MarchingCubes3D] exportOBJ failed: invalid filename.\n");
		return false;
	}
	if (!m_triangleDataValid || m_canonicalMesh.empty()) {
		printf("[MarchingCubes3D] exportOBJ failed: no canonical indexed mesh.\n");
		return false;
	}
	const bool normalsAvailable = writeNormals &&
		m_canonicalMesh.normals.size() == m_canonicalMesh.positions.size();
	const bool uvsAvailable =
		m_canonicalMesh.uvs.size() == m_canonicalMesh.positions.size();
	const std::filesystem::path objPath(filename);
	std::filesystem::path mtlPath = objPath;
	mtlPath.replace_extension(".mtl");

	ofstream out(filename);

	if (!out.is_open()) {
		printf(
			"[MarchingCubes3D] exportOBJ failed: could not open '%s'\n",
			filename
		);
		return false;
	}

	out << "# VitruGen SIMCAD canonical indexed Marching Cubes OBJ\n";
	out << "# Volume-workspace export with authored placement preserved\n";
	out << "# vertices: " << m_canonicalMesh.positions.size() << "\n";
	out << "# triangles: " << m_canonicalMesh.triangleCount() << "\n";
	out << "mtllib " << mtlPath.filename().generic_string() << "\n";
	out << "o SP_MCAD_MESH\n";
	out << "\n";

	out << fixed << setprecision(6);

	for (const vitru::Vec3& v : m_canonicalMesh.positions) {
		out << "v " << v.x << " " << v.y << " " << v.z << "\n";
	}

	out << "\n";

	if (normalsAvailable) {
		for (const vitru::Vec3& n : m_canonicalMesh.normals) {
			out << "vn " << n.x << " " << n.y << " " << n.z << "\n";
		}

		out << "\n";
	}
	if (uvsAvailable) {
		for (const vitru::Vec2& uv : m_canonicalMesh.uvs) {
			out << "vt " << uv.x << " " << uv.y << "\n";
		}
		out << "\n";
	}
	out << "usemtl VITRUGEN_DEFAULT\n";

	for (size_t tri = 0; tri < m_canonicalMesh.triangleCount(); ++tri) {
		const uint32_t i0 = m_canonicalMesh.indices[tri * 3u] + 1u;
		const uint32_t i1 = m_canonicalMesh.indices[tri * 3u + 1u] + 1u;
		const uint32_t i2 = m_canonicalMesh.indices[tri * 3u + 2u] + 1u;

		if (normalsAvailable && uvsAvailable) {
			out << "f "
				<< i0 << "/" << i0 << "/" << i0 << " "
				<< i1 << "/" << i1 << "/" << i1 << " "
				<< i2 << "/" << i2 << "/" << i2 << "\n";
		}
		else if (normalsAvailable) {
			out << "f "
				<< i0 << "//" << i0 << " "
				<< i1 << "//" << i1 << " "
				<< i2 << "//" << i2 << "\n";
		}
		else {
			out << "f "
				<< i0 << " "
				<< i1 << " "
				<< i2 << "\n";
		}
	}

	out.close();
	{
		ofstream material(mtlPath);
		if (material.is_open()) {
			material << "# VitruGen generated material\n"
				<< "newmtl VITRUGEN_DEFAULT\n"
				<< "Ka 0.15 0.15 0.15\n"
				<< "Kd 0.75 0.78 0.82\n"
				<< "Ks 0.0 0.0 0.0\n"
				<< "d 1.0\nillum 2\n";
		}
	}

	printf(
		"[MarchingCubes3D] exportOBJ success: '%s' vertices=%zu triangles=%zu normals=%s uvs=%s indexed=YES\n",
		filename,
		m_canonicalMesh.positions.size(),
		m_canonicalMesh.triangleCount(),
		normalsAvailable ? "YES" : "NO",
		uvsAvailable ? "YES" : "NO"
	);

	return true;
}

void MarchingCubes::rebuildCanonicalMesh() {
	std::vector<vitru::Vec3> raw;
	raw.reserve(m_triangleVertsCPU.size());
	for (const float4& p : m_triangleVertsCPU) {
		raw.push_back({ p.x, p.y, p.z });
	}
	vitru::MeshProcessingOptions options;
	const float minVoxel = (std::min)(m_voxelSize.x,
		(std::min)(m_voxelSize.y, m_voxelSize.z));
	options.weldEpsilon = (std::max)(1.0e-6f, minVoxel * 1.0e-4f);
	options.degenerateAreaEpsilon = options.weldEpsilon * options.weldEpsilon;
	m_meshProcessingReport = vitru::MeshProcessor::processTriangleSoup(
		raw, m_canonicalMesh, options);
	if (m_meshProcessingReport.success) {
		const vitru::MeshUVGenerationReport uvReport =
			vitru::generateBoxAtlasUVs(m_canonicalMesh);
		if (!uvReport.success) {
			printf("[MarchingCubes3D] WARNING: BOX_ATLAS_6_DIRECTION UV generation failed.\n");
		}
		else {
			printf("[MarchingCubes3D] UV atlas: policy=%s renderV=%zu seamDuplicates=%zu\n",
				uvReport.policy.c_str(), uvReport.outputVertices, uvReport.seamDuplicates);
		}
	}

	if (m_canonicalMesh.bounds.valid) {
		const vitru::MeshBounds& b = m_canonicalMesh.bounds;
		m_meshMin = make_float3(b.min.x, b.min.y, b.min.z);
		m_meshMax = make_float3(b.max.x, b.max.y, b.max.z);
		m_meshCenter = make_float3(b.center.x, b.center.y, b.center.z);
		m_meshBoundsValid = true;
	}
	printf(
		"[MarchingCubes3D] canonical mesh: rawV=%zu rawT=%zu finalV=%zu finalT=%zu nonfinite=%zu degenerate=%zu duplicate=%zu welded=%zu radius=%.6f\n",
		m_meshProcessingReport.rawVertexCount,
		m_meshProcessingReport.rawTriangleCount,
		m_meshProcessingReport.finalVertexCount,
		m_meshProcessingReport.finalTriangleCount,
		m_meshProcessingReport.removedNonFiniteTriangles,
		m_meshProcessingReport.removedDegenerateTriangles,
		m_meshProcessingReport.removedDuplicateTriangles,
		m_meshProcessingReport.weldedVertexInstances,
		m_meshProcessingReport.bounds.radius);
}

void MarchingCubes::shutdown() {
	if (!m_initialized) return;

	destroyMeshVBos();
	freeTriangleBuffers();

	freeArray(m_dVoxelVerts);
	freeArray(m_dVoxelVertsScan);
	freeArray(m_dVoxelOccupied);
	freeArray(m_dVoxelOccupiedScan);
	freeArray(m_dCompVoxelArray);

	m_dVoxelVerts = nullptr;
	m_dVoxelVertsScan = nullptr;
	m_dVoxelOccupied = nullptr;
	m_dVoxelOccupiedScan = nullptr;
	m_dCompVoxelArray = nullptr;

	freeMarchingCubesTables(
		m_dEdgeTable,
		m_dTriTable,
		m_dNumVertsTable
	);

	m_activeVoxels = 0;
	m_totalVerts = 0;
	m_canonicalMesh.clear();
	m_meshProcessingReport = vitru::MeshProcessingReport{};
	m_initialized = false;

	printf("[MarchingCubes3D] Shutdown complete.\n");
}
void MarchingCubes::classifyOnly(float* dVolume, float isoValue) {
	if (!m_initialized || !dVolume) return;

	MarchingCubesParams params{};
	params.volumeSize = m_gridSize;
	params.voxelSize = m_voxelSize;
	params.isoValue = isoValue;
	params.numCells = m_numVoxels;
	params.maxVerts = m_maxVerts;

	setMarchingCubesParameters(&params);

	const uint threadsPerBlock = 256;
	const uint numBlocks = iDivUp(m_numVoxels, threadsPerBlock);

	dim3 threads(threadsPerBlock, 1, 1);
	dim3 grid(numBlocks, 1, 1);

	launchMCClassifyVoxels(
		grid,
		threads,
		m_dVoxelVerts,
		m_dVoxelOccupied,
		dVolume,
		m_dNumVertsTable,
		m_gridSize,
		m_gridSizeShift,
		m_gridSizeMask,
		m_numVoxels,
		m_voxelSize,
		isoValue
	);

	mcThrustScanWrapper(
		m_dVoxelOccupiedScan,
		m_dVoxelOccupied,
		m_numVoxels
	);

	mcThrustScanWrapper(
		m_dVoxelVertsScan,
		m_dVoxelVerts,
		m_numVoxels
	);

	const uint last = m_numVoxels - 1u;

	uint lastOccupied = 0;
	uint lastOccupiedScan = 0;
	uint lastVerts = 0;
	uint lastVertsScan = 0;

	copyArrayFromDevice(
		&lastOccupied,
		m_dVoxelOccupied + last,
		nullptr,
		sizeof(uint)
	);

	copyArrayFromDevice(
		&lastOccupiedScan,
		m_dVoxelOccupiedScan + last,
		nullptr,
		sizeof(uint)
	);

	copyArrayFromDevice(
		&lastVerts,
		m_dVoxelVerts + last,
		nullptr,
		sizeof(uint)
	);

	copyArrayFromDevice(
		&lastVertsScan,
		m_dVoxelVertsScan + last,
		nullptr,
		sizeof(uint)
	);

	m_activeVoxels = lastOccupiedScan + lastOccupied;
	m_totalVerts = lastVertsScan + lastVerts;

	if (m_activeVoxels > 0) {
		launchMCCompactVoxels(
			grid,
			threads,
			m_dCompVoxelArray,
			m_dVoxelOccupied,
			m_dVoxelOccupiedScan,
			m_numVoxels
		);
	}

	updateActiveVoxelDebugBuffer();

	printf(
		"[MarchingCubes3D] classifyOnly: iso=%.4f, activeVoxels=%u, totalVerts=%u\n",
		isoValue,
		m_activeVoxels,
		m_totalVerts
	);
}
void MarchingCubes::generateTriangles(float* dVolume, float isoValue) {
	m_triangleDataValid = false;

	if (!m_initialized || !dVolume) return;
	if (m_activeVoxels == 0 || m_totalVerts == 0) {
		printf(
			"[MarchingCubes3D] generateTriangles skipped: activeVoxels=%u, totalVerts=%u\n",
			m_activeVoxels,
			m_totalVerts
		);
		return;
	}

	if (!ensureTriangleBuffers(m_totalVerts)) {
		printf("[MarchingCubes3D] ERROR: failed to allocate triangle buffers.\n");
		return;
	}

	MarchingCubesParams params{};
	params.volumeSize = m_gridSize;
	params.voxelSize = m_voxelSize;
	params.isoValue = isoValue;
	params.numCells = m_numVoxels;
	params.maxVerts = m_totalVerts;

	setMarchingCubesParameters(&params);

	const uint threadsPerBlock = NTHREADS;
	const uint numBlocks = iDivUp(m_activeVoxels, threadsPerBlock);

	dim3 threads(threadsPerBlock, 1, 1);
	dim3 grid(numBlocks, 1, 1);

	launchMCGenerateTriangles(
		grid,
		threads,
		m_dTriangleVerts,
		m_dTriangleNorms,
		m_dCompVoxelArray,
		m_dVoxelVertsScan,
		dVolume,
		m_dNumVertsTable,
		m_dTriTable,
		m_gridSize,
		m_gridSizeShift,
		m_gridSizeMask,
		m_voxelSize,
		isoValue,
		m_activeVoxels,
		m_totalVerts
	);

	threadSync();

	m_triangleDataValid = true;
	updateTriangleDebugBuffers();
	rebuildCanonicalMesh();
	m_triangleDataValid = m_meshProcessingReport.success;

	printf(
		"[MarchingCubes3D] generateTriangles: activeVoxels=%u, totalVerts=%u, triangles=%u\n",
		m_activeVoxels,
		m_totalVerts,
		m_totalVerts / 3u
	);

	debugPrintFirstTriangle();
}
void MarchingCubes::updateActiveVoxelDebugBuffer() {
	m_activeVoxelIdsCPU.clear();

	if (!m_initialized) return;
	if (!m_dCompVoxelArray) return;
	if (m_activeVoxels == 0) return;

	const uint copyCount =
		std::min(m_activeVoxels, m_debugActiveVoxelCopyLimit);

	if (copyCount == 0) return;

	m_activeVoxelIdsCPU.resize(copyCount);

	copyArrayFromDevice(
		m_activeVoxelIdsCPU.data(),
		m_dCompVoxelArray,
		nullptr,
		static_cast<int>(copyCount * sizeof(uint))
	);
}
void MarchingCubes::extract(float* dVolume, float isoValue) {
	m_triangleDataValid = false;
	m_meshBoundsValid = false;
	m_canonicalMesh.clear();
	m_meshProcessingReport = vitru::MeshProcessingReport{};

	m_triangleVertsCPU.clear();
	m_triangleNormsCPU.clear();

	if (!m_initialized || !dVolume) {
		return;
	}

	// Step 1:
	// Classify voxels, scan occupied cells, scan vertex counts,
	// compact active voxels, and compute m_activeVoxels / m_totalVerts.
	classifyOnly(dVolume, isoValue);

	if (m_activeVoxels == 0 || m_totalVerts == 0) {
		printf(
			"[MarchingCubes3D] extract skipped: activeVoxels=%u, totalVerts=%u\n",
			m_activeVoxels,
			m_totalVerts
		);
		return;
	}

	// Step 2:
	// Make sure the OpenGL/CUDA VBOs can hold the generated triangle soup.
	createMeshVBOs();

	if (!m_posVbo ||
		!m_normVbo ||
		!m_cudaPosVboResource ||
		!m_cudaNormVboResource) {

		printf("[MarchingCubes3D] ERROR: mesh VBOs are not available.\n");
		return;
	}

	float4* dPos = reinterpret_cast<float4*>(mapGLBufferObject(&m_cudaPosVboResource));
	float4* dNorm = reinterpret_cast<float4*>(mapGLBufferObject(&m_cudaNormVboResource));

	if (!dPos || !dNorm) {
		printf("[MarchingCubes3D] ERROR: failed to map mesh VBOs.\n");

		if (m_cudaPosVboResource) {
			unmapGLBufferObject(m_cudaPosVboResource);
		}

		if (m_cudaNormVboResource) {
			unmapGLBufferObject(m_cudaNormVboResource);
		}

		return;
	}

	MarchingCubesParams params{};
	params.volumeSize = m_gridSize;
	params.voxelSize = m_voxelSize;
	params.isoValue = isoValue;
	params.numCells = m_numVoxels;
	params.maxVerts = m_totalVerts;

	setMarchingCubesParameters(&params);

	const uint threadsPerBlock = NTHREADS;
	const uint numBlocks = iDivUp(m_activeVoxels, threadsPerBlock);

	dim3 threads(threadsPerBlock, 1, 1);
	dim3 grid(numBlocks, 1, 1);

	launchMCGenerateTriangles(
		grid,
		threads,
		dPos,
		dNorm,
		m_dCompVoxelArray,
		m_dVoxelVertsScan,
		dVolume,
		m_dNumVertsTable,
		m_dTriTable,
		m_gridSize,
		m_gridSizeShift,
		m_gridSizeMask,
		m_voxelSize,
		isoValue,
		m_activeVoxels,
		m_totalVerts
	);

	threadSync();

	//float4 hTest[3];

	std::vector<float4> hVerts(m_totalVerts);

	copyArrayFromDevice(
		hVerts.data(),
		dPos,
		nullptr,
		static_cast<int>(m_totalVerts * sizeof(float4))
	);

	if (!hVerts.empty()) {
		float minX = hVerts[0].x;
		float minY = hVerts[0].y;
		float minZ = hVerts[0].z;

		float maxX = hVerts[0].x;
		float maxY = hVerts[0].y;
		float maxZ = hVerts[0].z;

		for (uint i = 1; i < m_totalVerts; ++i) {
			minX = std::min(minX, hVerts[i].x);
			minY = std::min(minY, hVerts[i].y);
			minZ = std::min(minZ, hVerts[i].z);

			maxX = std::max(maxX, hVerts[i].x);
			maxY = std::max(maxY, hVerts[i].y);
			maxZ = std::max(maxZ, hVerts[i].z);
		}

		m_meshMin = make_float3(minX, minY, minZ);
		m_meshMax = make_float3(maxX, maxY, maxZ);

		m_meshCenter = make_float3(
			0.5f * (minX + maxX),
			0.5f * (minY + maxY),
			0.5f * (minZ + maxZ)
		);

		m_meshBoundsValid = true;

		printf(
			"[MarchingCubes3D] mesh bounds:\n"
			"  min    = %.4f %.4f %.4f\n"
			"  max    = %.4f %.4f %.4f\n"
			"  center = %.4f %.4f %.4f\n",
			m_meshMin.x, m_meshMin.y, m_meshMin.z,
			m_meshMax.x, m_meshMax.y, m_meshMax.z,
			m_meshCenter.x, m_meshCenter.y, m_meshCenter.z
		);
	}

	m_triangleVertsCPU.clear();
	m_triangleNormsCPU.clear();

	m_triangleVertsCPU.resize(m_totalVerts);
	m_triangleNormsCPU.resize(m_totalVerts);

	copyArrayFromDevice(
		m_triangleVertsCPU.data(),
		dPos,
		nullptr,
		static_cast<int>(m_totalVerts * sizeof(float4))
	);

	copyArrayFromDevice(
		m_triangleNormsCPU.data(),
		dNorm,
		nullptr,
		static_cast<int>(m_totalVerts * sizeof(float4))
	);
	rebuildCanonicalMesh();


	unmapGLBufferObject(m_cudaPosVboResource);
	unmapGLBufferObject(m_cudaNormVboResource);

	m_triangleDataValid = m_meshProcessingReport.success;

	printf(
		"[MarchingCubes3D] extract: activeVoxels=%u, totalVerts=%u, triangles=%u\n",
		m_activeVoxels,
		m_totalVerts,
		m_totalVerts / 3u
	);
}
void MarchingCubes::createMeshVBOs() {
	if (m_totalVerts == 0) {
		m_triangleDataValid = false;
		return;
	}

	// Existing VBOs are large enough; keep them.
	if (m_posVbo &&
		m_normVbo &&
		m_cudaPosVboResource &&
		m_cudaNormVboResource &&
		m_allocatedMeshVerts >= m_totalVerts) {
		return;
	}

	// Reallocate if missing or too small.
	destroyMeshVBos();

	const size_t bytes =
		static_cast<size_t>(m_totalVerts) * sizeof(float4);

	glGenBuffers(1, &m_posVbo);
	glBindBuffer(GL_ARRAY_BUFFER, m_posVbo);
	glBufferData(
		GL_ARRAY_BUFFER,
		bytes,
		nullptr,
		GL_DYNAMIC_DRAW
	);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glGenBuffers(1, &m_normVbo);
	glBindBuffer(GL_ARRAY_BUFFER, m_normVbo);
	glBufferData(
		GL_ARRAY_BUFFER,
		bytes,
		nullptr,
		GL_DYNAMIC_DRAW
	);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	registerGLBufferObject(
		m_posVbo,
		&m_cudaPosVboResource
	);

	registerGLBufferObject(
		m_normVbo,
		&m_cudaNormVboResource
	);

	m_allocatedMeshVerts = m_totalVerts;

	printf(
		"[MarchingCubes3D] Created mesh VBOs: verts=%u, bytes=%zu each\n",
		m_allocatedMeshVerts,
		bytes
	);
}
void MarchingCubes::destroyMeshVBos() {
	if (m_cudaPosVboResource) {
		unregisterGLBufferObject(m_cudaPosVboResource);
		m_cudaPosVboResource = nullptr;
	}

	if (m_cudaNormVboResource) {
		unregisterGLBufferObject(m_cudaNormVboResource);
		m_cudaNormVboResource = nullptr;
	}

	if (m_posVbo) {
		glDeleteBuffers(1, &m_posVbo);
		m_posVbo = 0;
	}

	if (m_normVbo) {
		glDeleteBuffers(1, &m_normVbo);
		m_normVbo = 0;
	}

	m_allocatedMeshVerts = 0;
	m_triangleDataValid = false;
}
void MarchingCubes::renderWireframe(float thetaRad, float phiRad, float zs) {
	if (!m_triangleDataValid) return;
	if (!m_posVbo) return;
	if (m_totalVerts == 0) return;

	const int winW = std::max(1, glutGet(GLUT_WINDOW_WIDTH));
	const int winH = std::max(1, glutGet(GLUT_WINDOW_HEIGHT));

	const float aspect =
		static_cast<float>(winW) / static_cast<float>(winH);

	glViewport(0, 0, winW, winH);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();

	gluPerspective(
		60.0,
		static_cast<double>(aspect),
		1.0,
		2000.0
	);

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	// Match the CUDA raycast camera.
	glTranslatef(0.0f, 0.0f, -zs);

	glRotatef(
		phiRad * 180.0f / 3.1415926535f,
		1.0f, 0.0f, 0.0f
	);

	glRotatef(
		thetaRad * 180.0f / 3.1415926535f,
		0.0f, 1.0f, 0.0f
	);

	// MC vertices are already expressed in volume-workspace coordinates.
	//
	// Do not translate by -m_meshCenter here. The mesh center contains
	// the authored Node_2 offset, so recentering would erase placement.
	glUseProgram(0);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_LIGHTING);

	// The volume render is a screen-space texture, not depth geometry.
	// Disable depth for first wireframe overlay pass so the mesh is visible.
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glColor4f(0.65f, 0.92f, 1.0f, 0.88f);
	glLineWidth(1.0f);

	glBindBuffer(GL_ARRAY_BUFFER, m_posVbo);

	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(4,GL_FLOAT,0,nullptr);
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_totalVerts));
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glDisableClientState(GL_VERTEX_ARRAY);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glLineWidth(1.0f);

	glDisable(GL_BLEND);
	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);

	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();

	glMatrixMode(GL_PROJECTION);
	glPopMatrix();

	glMatrixMode(GL_MODELVIEW);
}
