#include "MeshUVGenerator.h"

#include "PngImage.h"
#include "StaticParticleAsset.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace vitru {
namespace {

Vec3 subtract(const Vec3& a, const Vec3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
Vec3 cross(const Vec3& a, const Vec3& b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }

int faceCell(const Vec3& normal) {
	const float ax = std::fabs(normal.x), ay = std::fabs(normal.y), az = std::fabs(normal.z);
	if (ax >= ay && ax >= az) return normal.x >= 0.0f ? 0 : 1;
	if (ay >= az) return normal.y >= 0.0f ? 2 : 3;
	return normal.z >= 0.0f ? 4 : 5;
}

float normalized(float value, float minimum, float extent) {
	return extent > 1.0e-12f ? (value - minimum) / extent : 0.5f;
}

Vec2 project(const Vec3& p, const MeshBounds& bounds, int cell, float padding) {
	float a = 0.5f, b = 0.5f;
	switch (cell) {
	case 0: a = normalized(-p.z, -bounds.max.z, bounds.extent.z); b = normalized(p.y, bounds.min.y, bounds.extent.y); break;
	case 1: a = normalized(p.z, bounds.min.z, bounds.extent.z); b = normalized(p.y, bounds.min.y, bounds.extent.y); break;
	case 2: a = normalized(p.x, bounds.min.x, bounds.extent.x); b = normalized(-p.z, -bounds.max.z, bounds.extent.z); break;
	case 3: a = normalized(p.x, bounds.min.x, bounds.extent.x); b = normalized(p.z, bounds.min.z, bounds.extent.z); break;
	case 4: a = normalized(p.x, bounds.min.x, bounds.extent.x); b = normalized(p.y, bounds.min.y, bounds.extent.y); break;
	default: a = normalized(-p.x, -bounds.max.x, bounds.extent.x); b = normalized(p.y, bounds.min.y, bounds.extent.y); break;
	}
	a = padding + (1.0f - 2.0f * padding) * (std::max)(0.0f, (std::min)(1.0f, a));
	b = padding + (1.0f - 2.0f * padding) * (std::max)(0.0f, (std::min)(1.0f, b));
	const int column = cell % 3;
	const int row = cell / 3;
	return { (static_cast<float>(column) + a) / 3.0f, (static_cast<float>(row) + b) / 2.0f };
}

struct SeamKey {
	std::uint32_t source = 0;
	std::uint32_t cell = 0;
	bool operator==(const SeamKey& other) const { return source == other.source && cell == other.cell; }
};
struct SeamHash {
	std::size_t operator()(const SeamKey& key) const { return (static_cast<std::size_t>(key.source) << 3u) ^ key.cell; }
};

void putPixel(ImageRGBA8& image, int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
	if (x < 0 || y < 0 || x >= static_cast<int>(image.width) || y >= static_cast<int>(image.height)) return;
	const std::size_t offset = (static_cast<std::size_t>(y) * image.width + static_cast<std::size_t>(x)) * 4u;
	image.pixels[offset] = r; image.pixels[offset + 1u] = g; image.pixels[offset + 2u] = b; image.pixels[offset + 3u] = 255u;
}
void line(ImageRGBA8& image, int x0, int y0, int x1, int y1, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
	const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
	const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
	int error = dx + dy;
	for (;;) {
		putPixel(image, x0, y0, r, g, b);
		if (x0 == x1 && y0 == y1) break;
		const int e2 = 2 * error;
		if (e2 >= dy) { error += dy; x0 += sx; }
		if (e2 <= dx) { error += dx; y0 += sy; }
	}
}

} // namespace

MeshUVGenerationReport generateBoxAtlasUVs(MeshGeometry& mesh, float cellPadding) {
	MeshUVGenerationReport report; report.inputVertices = mesh.positions.size();
	if (mesh.empty() || (mesh.indices.size() % 3u) != 0u) { report.errors.push_back("UV generation requires an indexed triangle mesh."); return report; }
	for (std::uint32_t index : mesh.indices) if (index >= mesh.positions.size()) { report.errors.push_back("UV generation encountered an invalid index."); return report; }
	const MeshBounds bounds = mesh.bounds.valid ? mesh.bounds : calculateMeshBounds(mesh);
	if (!bounds.valid) { report.errors.push_back("UV generation requires valid mesh bounds."); return report; }
	cellPadding = (std::max)(0.0f, (std::min)(0.20f, cellPadding));

	MeshGeometry output; output.indices.reserve(mesh.indices.size());
	std::unordered_map<SeamKey, std::uint32_t, SeamHash> remap;
	for (std::size_t tri = 0; tri < mesh.indices.size(); tri += 3u) {
		const std::uint32_t ia = mesh.indices[tri], ib = mesh.indices[tri + 1u], ic = mesh.indices[tri + 2u];
		const Vec3 face = cross(subtract(mesh.positions[ib], mesh.positions[ia]), subtract(mesh.positions[ic], mesh.positions[ia]));
		const int cell = faceCell(face);
		for (std::uint32_t source : { ia, ib, ic }) {
			const SeamKey key{ source, static_cast<std::uint32_t>(cell) };
			auto found = remap.find(key);
			if (found == remap.end()) {
				const std::uint32_t target = static_cast<std::uint32_t>(output.positions.size());
				output.positions.push_back(mesh.positions[source]);
				output.normals.push_back(mesh.normals.size() == mesh.positions.size() ? mesh.normals[source] : Vec3{});
				output.uvs.push_back(project(mesh.positions[source], bounds, cell, cellPadding));
				if (mesh.uvs1.size() == mesh.positions.size()) output.uvs1.push_back(mesh.uvs1[source]);
				if (mesh.tangents.size() == mesh.positions.size()) output.tangents.push_back(mesh.tangents[source]);
				found = remap.emplace(key, target).first;
			}
			output.indices.push_back(found->second);
		}
	}
	output.bounds = bounds;
	mesh = std::move(output);
	report.outputVertices = mesh.positions.size();
	report.seamDuplicates = report.outputVertices > report.inputVertices ? report.outputVertices - report.inputVertices : 0u;
	report.success = mesh.uvs.size() == mesh.positions.size() && !mesh.empty();
	return report;
}

bool writeUvGuidePng(const MeshGeometry& mesh, const std::filesystem::path& path,
	std::uint32_t size, std::string* error) {
	if (size < 64u || mesh.uvs.size() != mesh.positions.size() || mesh.indices.empty()) {
		if (error) *error = "UV guide requires valid TEXCOORD_0 data and indexed triangles.";
		return false;
	}
	ImageRGBA8 image = makeSolidImage(size, size, 52u, 58u, 64u, 255u);
	const std::uint8_t cellColors[6][3] = { {225,95,80}, {225,155,80}, {205,205,80}, {90,190,120}, {80,155,225}, {175,105,225} };
	for (int row = 0; row < 2; ++row) for (int col = 0; col < 3; ++col) {
		const int cell = row * 3 + col;
		const int x0 = col * static_cast<int>(size) / 3;
		const int x1 = (col + 1) * static_cast<int>(size) / 3 - 1;
		const int y0 = row * static_cast<int>(size) / 2;
		const int y1 = (row + 1) * static_cast<int>(size) / 2 - 1;
		line(image, x0, y0, x1, y0, cellColors[cell][0], cellColors[cell][1], cellColors[cell][2]);
		line(image, x1, y0, x1, y1, cellColors[cell][0], cellColors[cell][1], cellColors[cell][2]);
		line(image, x1, y1, x0, y1, cellColors[cell][0], cellColors[cell][1], cellColors[cell][2]);
		line(image, x0, y1, x0, y0, cellColors[cell][0], cellColors[cell][1], cellColors[cell][2]);
	}
	for (std::size_t i = 0; i + 2u < mesh.indices.size(); i += 3u) {
		int x[3], y[3]; bool valid = true;
		for (int k = 0; k < 3; ++k) {
			const std::uint32_t index = mesh.indices[i + static_cast<std::size_t>(k)];
			if (index >= mesh.uvs.size()) { valid = false; break; }
			x[k] = static_cast<int>(std::lround(mesh.uvs[index].x * static_cast<float>(size - 1u)));
			y[k] = static_cast<int>(std::lround((1.0f - mesh.uvs[index].y) * static_cast<float>(size - 1u)));
		}
		if (!valid) continue;
		line(image, x[0], y[0], x[1], y[1], 220u, 235u, 240u);
		line(image, x[1], y[1], x[2], y[2], 220u, 235u, 240u);
		line(image, x[2], y[2], x[0], y[0], 220u, 235u, 240u);
	}
	return writePngImage(path, image, error);
}

} // namespace vitru
