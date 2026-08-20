#ifndef VITRUGEN_MESH_PROCESSOR_H
#define VITRUGEN_MESH_PROCESSOR_H

#include "MeshGeometry.h"

#include <cstddef>
#include <vector>

namespace vitru {

struct MeshProcessingOptions {
	float weldEpsilon = 1.0e-5f;
	float degenerateAreaEpsilon = 1.0e-10f;
};

struct MeshProcessingReport {
	std::size_t rawVertexCount = 0;
	std::size_t rawTriangleCount = 0;
	std::size_t removedNonFiniteTriangles = 0;
	std::size_t removedDegenerateTriangles = 0;
	std::size_t removedDuplicateTriangles = 0;
	std::size_t weldedVertexInstances = 0;
	std::size_t finalVertexCount = 0;
	std::size_t finalTriangleCount = 0;
	MeshBounds bounds;
	bool valid = false;
	bool success = false;
};

class MeshProcessor {
public:
	static MeshProcessingReport processTriangleSoup(
		const std::vector<Vec3>& rawPositions,
		MeshGeometry& output,
		const MeshProcessingOptions& options = MeshProcessingOptions{});
};

} // namespace vitru

#endif
