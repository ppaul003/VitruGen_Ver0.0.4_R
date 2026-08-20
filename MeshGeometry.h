#ifndef VITRUGEN_MESH_GEOMETRY_H
#define VITRUGEN_MESH_GEOMETRY_H

#include <cstdint>
#include <vector>

namespace vitru {

struct Vec2 {
	float x = 0.0f;
	float y = 0.0f;
};

struct Vec3 {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
};

struct MeshBounds {
	Vec3 min;
	Vec3 max;
	Vec3 center;
	Vec3 extent;
	float maxAxisExtent = 0.0f;
	float radius = 0.0f;
	bool valid = false;
};

struct MeshGeometry {
	std::vector<Vec3> positions;
	std::vector<Vec3> normals;
	std::vector<Vec2> uvs;
	// Reserved for future baked-light/lightmap coordinates.
	std::vector<Vec2> uvs1;
	// Reserved for future normal-map rendering.
	std::vector<Vec3> tangents;
	std::vector<std::uint32_t> indices;
	MeshBounds bounds;

	void clear();
	bool empty() const { return positions.empty() || indices.empty(); }
	std::size_t triangleCount() const { return indices.size() / 3u; }
};

} // namespace vitru

#endif
