#include "MeshProcessor.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

int main() {
	using vitru::Vec3;
	std::vector<Vec3> soup = {
		// Two valid tetrahedron faces sharing weldable vertices.
		{0,0,0}, {1,0,0}, {0,1,0},
		{0,0,0}, {0,1,0}, {0,0,1},
		// Exact duplicate with reversed winding.
		{0,1,0}, {1,0,0}, {0,0,0},
		// Degenerate triangle.
		{0,0,0}, {0,0,0}, {1,1,1},
		// Non-finite triangle.
		{0,0,0}, {std::numeric_limits<float>::quiet_NaN(),0,0}, {0,1,0}
	};

	vitru::MeshGeometry mesh;
	vitru::MeshProcessingOptions options;
	options.weldEpsilon = 1.0e-5f;
	options.degenerateAreaEpsilon = 1.0e-10f;
	const vitru::MeshProcessingReport report =
		vitru::MeshProcessor::processTriangleSoup(soup, mesh, options);

	assert(report.success);
	assert(report.rawTriangleCount == 5u);
	assert(report.finalTriangleCount == 2u);
	assert(report.removedDuplicateTriangles == 1u);
	assert(report.removedDegenerateTriangles == 1u);
	assert(report.removedNonFiniteTriangles == 1u);
	assert(mesh.positions.size() == 4u);
	assert(mesh.indices.size() == 6u);
	assert(mesh.normals.size() == mesh.positions.size());
	assert(mesh.uvs.empty());
	assert(mesh.bounds.valid);
	for (std::uint32_t index : mesh.indices) {
		assert(index < mesh.positions.size());
	}
	for (const Vec3& n : mesh.normals) {
		const float length = std::sqrt(n.x*n.x + n.y*n.y + n.z*n.z);
		assert(std::isfinite(length));
		assert(std::fabs(length - 1.0f) < 1.0e-4f);
	}

	const Vec3 p000{0,0,0}, p100{1,0,0}, p110{1,1,0}, p010{0,1,0};
	const Vec3 p001{0,0,1}, p101{1,0,1}, p111{1,1,1}, p011{0,1,1};
	const std::vector<Vec3> cube = {
		p000,p110,p100, p000,p010,p110,
		p001,p101,p111, p001,p111,p011,
		p000,p100,p101, p000,p101,p001,
		p010,p011,p111, p010,p111,p110,
		p000,p001,p011, p000,p011,p010,
		p100,p110,p111, p100,p111,p101
	};
	vitru::MeshGeometry cubeMesh;
	const vitru::MeshProcessingReport cubeReport =
		vitru::MeshProcessor::processTriangleSoup(cube, cubeMesh, options);
	assert(cubeReport.success);
	assert(cubeReport.finalVertexCount == 8u);
	assert(cubeReport.finalTriangleCount == 12u);
	assert(cubeMesh.indices.size() == 36u);
	assert(cubeMesh.bounds.valid);
	assert(cubeMesh.bounds.min.x == 0.0f && cubeMesh.bounds.min.y == 0.0f && cubeMesh.bounds.min.z == 0.0f);
	assert(cubeMesh.bounds.max.x == 1.0f && cubeMesh.bounds.max.y == 1.0f && cubeMesh.bounds.max.z == 1.0f);
	for (std::uint32_t index : cubeMesh.indices) assert(index < cubeMesh.positions.size());
	for (const Vec3& n : cubeMesh.normals) {
		const float length = std::sqrt(n.x*n.x + n.y*n.y + n.z*n.z);
		assert(std::isfinite(length) && std::fabs(length - 1.0f) < 1.0e-4f);
	}

	std::printf(
		"MeshProcessorTests PASS malformedRawT=%zu malformedFinalT=%zu cubeV=%zu cubeI=%zu\n",
		report.rawTriangleCount,
		report.finalTriangleCount,
		cubeMesh.positions.size(),
		cubeMesh.indices.size());
	return 0;
}
