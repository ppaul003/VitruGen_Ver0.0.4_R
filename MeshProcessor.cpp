#include "MeshProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace vitru {
namespace {

Vec3 subtract(const Vec3& a, const Vec3& b) {
	return { a.x - b.x, a.y - b.y, a.z - b.z };
}

Vec3 cross(const Vec3& a, const Vec3& b) {
	return {
		a.y * b.z - a.z * b.y,
		a.z * b.x - a.x * b.z,
		a.x * b.y - a.y * b.x
	};
}

float lengthSquared(const Vec3& v) {
	return v.x * v.x + v.y * v.y + v.z * v.z;
}

bool finite(const Vec3& v) {
	return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

struct QuantizedVertex {
	std::int64_t x;
	std::int64_t y;
	std::int64_t z;
	bool operator==(const QuantizedVertex& rhs) const {
		return x == rhs.x && y == rhs.y && z == rhs.z;
	}
};

struct QuantizedVertexHash {
	std::size_t operator()(const QuantizedVertex& key) const {
		std::size_t h = std::hash<std::int64_t>{}(key.x);
		h ^= std::hash<std::int64_t>{}(key.y) + 0x9e3779b9u + (h << 6) + (h >> 2);
		h ^= std::hash<std::int64_t>{}(key.z) + 0x9e3779b9u + (h << 6) + (h >> 2);
		return h;
	}
};

struct TriangleKey {
	std::uint32_t a;
	std::uint32_t b;
	std::uint32_t c;
	bool operator==(const TriangleKey& rhs) const {
		return a == rhs.a && b == rhs.b && c == rhs.c;
	}
};

struct TriangleKeyHash {
	std::size_t operator()(const TriangleKey& key) const {
		std::size_t h = std::hash<std::uint32_t>{}(key.a);
		h ^= std::hash<std::uint32_t>{}(key.b) + 0x9e3779b9u + (h << 6) + (h >> 2);
		h ^= std::hash<std::uint32_t>{}(key.c) + 0x9e3779b9u + (h << 6) + (h >> 2);
		return h;
	}
};

QuantizedVertex quantize(const Vec3& p, float epsilon) {
	return {
		static_cast<std::int64_t>(std::llround(p.x / epsilon)),
		static_cast<std::int64_t>(std::llround(p.y / epsilon)),
		static_cast<std::int64_t>(std::llround(p.z / epsilon))
	};
}

void computeBounds(MeshGeometry& mesh) {
	if (mesh.positions.empty()) return;
	Vec3 lo = mesh.positions.front();
	Vec3 hi = lo;
	for (const Vec3& p : mesh.positions) {
		lo.x = (std::min)(lo.x, p.x); lo.y = (std::min)(lo.y, p.y); lo.z = (std::min)(lo.z, p.z);
		hi.x = (std::max)(hi.x, p.x); hi.y = (std::max)(hi.y, p.y); hi.z = (std::max)(hi.z, p.z);
	}
	mesh.bounds.min = lo;
	mesh.bounds.max = hi;
	mesh.bounds.center = { 0.5f * (lo.x + hi.x), 0.5f * (lo.y + hi.y), 0.5f * (lo.z + hi.z) };
	mesh.bounds.extent = { hi.x - lo.x, hi.y - lo.y, hi.z - lo.z };
	mesh.bounds.maxAxisExtent = (std::max)(mesh.bounds.extent.x,
		(std::max)(mesh.bounds.extent.y, mesh.bounds.extent.z));
	float radiusSq = 0.0f;
	for (const Vec3& p : mesh.positions) {
		radiusSq = (std::max)(radiusSq, lengthSquared(subtract(p, mesh.bounds.center)));
	}
	mesh.bounds.radius = std::sqrt(radiusSq);
	mesh.bounds.valid = true;
}

} // namespace

MeshProcessingReport MeshProcessor::processTriangleSoup(
	const std::vector<Vec3>& rawPositions,
	MeshGeometry& output,
	const MeshProcessingOptions& options) {
	output.clear();
	MeshProcessingReport report;
	report.rawVertexCount = rawPositions.size();
	report.rawTriangleCount = rawPositions.size() / 3u;
	const float weldEpsilon = (std::max)(options.weldEpsilon, 1.0e-8f);
	const float areaEpsilonSq = options.degenerateAreaEpsilon * options.degenerateAreaEpsilon;

	std::unordered_map<QuantizedVertex, std::uint32_t, QuantizedVertexHash> welded;
	std::unordered_set<TriangleKey, TriangleKeyHash> uniqueTriangles;
	auto vertexIndex = [&](const Vec3& p) {
		const QuantizedVertex key = quantize(p, weldEpsilon);
		const auto found = welded.find(key);
		if (found != welded.end()) {
			++report.weldedVertexInstances;
			return found->second;
		}
		const std::uint32_t index = static_cast<std::uint32_t>(output.positions.size());
		output.positions.push_back(p);
		welded.emplace(key, index);
		return index;
	};

	for (std::size_t i = 0; i + 2 < rawPositions.size(); i += 3) {
		const Vec3& a = rawPositions[i];
		const Vec3& b = rawPositions[i + 1];
		const Vec3& c = rawPositions[i + 2];
		if (!finite(a) || !finite(b) || !finite(c)) {
			++report.removedNonFiniteTriangles;
			continue;
		}
		if (lengthSquared(cross(subtract(b, a), subtract(c, a))) <= areaEpsilonSq) {
			++report.removedDegenerateTriangles;
			continue;
		}
		const std::uint32_t ia = vertexIndex(a);
		const std::uint32_t ib = vertexIndex(b);
		const std::uint32_t ic = vertexIndex(c);
		if (ia == ib || ib == ic || ia == ic) {
			++report.removedDegenerateTriangles;
			continue;
		}
		std::uint32_t sorted[3] = { ia, ib, ic };
		std::sort(sorted, sorted + 3);
		const TriangleKey key{ sorted[0], sorted[1], sorted[2] };
		if (!uniqueTriangles.insert(key).second) {
			++report.removedDuplicateTriangles;
			continue;
		}
		output.indices.push_back(ia);
		output.indices.push_back(ib);
		output.indices.push_back(ic);
	}

	// Remove any welded vertices referenced only by triangles that collapsed.
	std::vector<std::uint32_t> remap(output.positions.size(), UINT32_MAX);
	std::vector<Vec3> compact;
	compact.reserve(output.positions.size());
	for (std::uint32_t& index : output.indices) {
		if (remap[index] == UINT32_MAX) {
			remap[index] = static_cast<std::uint32_t>(compact.size());
			compact.push_back(output.positions[index]);
		}
		index = remap[index];
	}
	output.positions.swap(compact);

	// Area-weighted smooth normals: the unnormalized face cross product is
	// accumulated before one normalization per welded vertex.
	output.normals.assign(output.positions.size(), Vec3{});
	for (std::size_t i = 0; i + 2 < output.indices.size(); i += 3) {
		const std::uint32_t ia = output.indices[i];
		const std::uint32_t ib = output.indices[i + 1];
		const std::uint32_t ic = output.indices[i + 2];
		const Vec3 face = cross(
			subtract(output.positions[ib], output.positions[ia]),
			subtract(output.positions[ic], output.positions[ia]));
		for (std::uint32_t index : { ia, ib, ic }) {
			output.normals[index].x += face.x;
			output.normals[index].y += face.y;
			output.normals[index].z += face.z;
		}
	}
	for (Vec3& normal : output.normals) {
		const float lenSq = lengthSquared(normal);
		if (lenSq > 1.0e-20f) {
			const float invLen = 1.0f / std::sqrt(lenSq);
			normal.x *= invLen; normal.y *= invLen; normal.z *= invLen;
		}
	}
	output.uvs.clear();
	computeBounds(output);
	report.finalVertexCount = output.positions.size();
	report.finalTriangleCount = output.triangleCount();
	report.success = !output.empty();
	report.bounds = output.bounds;
	report.valid = report.success && output.bounds.valid;
	return report;
}

} // namespace vitru
