#include "StaticParticleAsset.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>
#include <unordered_set>

namespace vitru {
namespace {

bool finite(float value) { return std::isfinite(value) != 0; }
bool finite(const Vec3& value) {
	return finite(value.x) && finite(value.y) && finite(value.z);
}
void addError(std::vector<std::string>* errors, const std::string& value) {
	if (errors) errors->push_back(value);
}
std::string upper(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
		return static_cast<char>(std::toupper(c));
	});
	return value;
}

} // namespace

MeshBounds calculateMeshBounds(const MeshGeometry& mesh) {
	MeshBounds result;
	if (mesh.positions.empty()) return result;
	Vec3 lo = mesh.positions.front();
	Vec3 hi = lo;
	for (const Vec3& p : mesh.positions) {
		if (!finite(p)) return MeshBounds{};
		lo.x = (std::min)(lo.x, p.x); lo.y = (std::min)(lo.y, p.y); lo.z = (std::min)(lo.z, p.z);
		hi.x = (std::max)(hi.x, p.x); hi.y = (std::max)(hi.y, p.y); hi.z = (std::max)(hi.z, p.z);
	}
	result.min = lo;
	result.max = hi;
	result.center = { 0.5f * (lo.x + hi.x), 0.5f * (lo.y + hi.y), 0.5f * (lo.z + hi.z) };
	result.extent = { hi.x - lo.x, hi.y - lo.y, hi.z - lo.z };
	result.maxAxisExtent = (std::max)(result.extent.x,
		(std::max)(result.extent.y, result.extent.z));
	float radiusSquared = 0.0f;
	for (const Vec3& p : mesh.positions) {
		const float x = p.x - result.center.x;
		const float y = p.y - result.center.y;
		const float z = p.z - result.center.z;
		radiusSquared = (std::max)(radiusSquared, x * x + y * y + z * z);
	}
	result.radius = std::sqrt(radiusSquared);
	result.valid = finite(result.min) && finite(result.max) &&
		finite(result.center) && finite(result.extent) &&
		finite(result.radius) && result.maxAxisExtent >= 0.0f;
	return result;
}

bool StaticParticleAsset::validate(std::vector<std::string>* errors) const {
	bool ok = true;
	auto reject = [&](const std::string& message) { ok = false; addError(errors, message); };
	if (name.empty()) reject("Static particle requires a display name.");
	if (mesh.empty()) reject("Static particle has no mesh.");
	if ((mesh.indices.size() % 3u) != 0u) reject("Mesh index count is not divisible by three.");
	for (std::uint32_t index : mesh.indices) {
		if (index >= mesh.positions.size()) { reject("Mesh contains an invalid vertex index."); break; }
	}
	if (!mesh.normals.empty() && mesh.normals.size() != mesh.positions.size())
		reject("Mesh normals do not match the render-vertex count.");
	if (!mesh.uvs.empty() && mesh.uvs.size() != mesh.positions.size())
		reject("Mesh TEXCOORD_0 data does not match the render-vertex count.");
	if (!mesh.uvs1.empty() && mesh.uvs1.size() != mesh.positions.size())
		reject("Mesh TEXCOORD_1 data does not match the render-vertex count.");
	for (const SubMesh& submesh : submeshes) {
		const std::uint64_t end = static_cast<std::uint64_t>(submesh.firstIndex) + submesh.indexCount;
		if (submesh.indexCount == 0u || (submesh.indexCount % 3u) != 0u || end > mesh.indices.size())
			reject("Submesh range is invalid: " + submesh.name);
		if (submesh.materialIndex >= materials.size())
			reject("Submesh material index is invalid: " + submesh.name);
	}
	std::unordered_set<std::string> textureIds;
	for (const TextureResource& texture : textures) {
		if (texture.id.empty()) reject("Texture resource has no stable ID.");
		else if (!textureIds.insert(texture.id).second) reject("Duplicate texture ID: " + texture.id);
	}
	if (materials.empty()) reject("Asset has no material/base-color fallback.");
	for (const MaterialSlot& material : materials) {
		if (!material.baseColorTextureId.empty() && !findTexture(material.baseColorTextureId))
			reject("Material references a missing base-color texture: " + material.baseColorTextureId);
		if (!material.emissiveTextureId.empty() && !findTexture(material.emissiveTextureId))
			reject("Material references a missing emissive texture: " + material.emissiveTextureId);
		if (!std::isfinite(material.emissiveIntensity) ||
			material.emissiveIntensity < 0.0f || material.emissiveIntensity > 4.0f)
			reject("Material emissive intensity must be between 0.0 and 4.0.");
	}
	std::unordered_set<std::string> surfaceTargetNames;
	for (const SurfaceTarget& target : surfaceTargets) {
		if (target.name.empty()) reject("Surface target requires a name.");
		else if (!surfaceTargetNames.insert(upper(target.name)).second)
			reject("Duplicate surface target name: " + target.name);
		if (target.faceIndex >= 6u) reject("Surface target face index is outside BOX_ATLAS.");
		if (target.normalizedPolygon.size() < 3u)
			reject("Surface target requires at least three contour points: " + target.name);
		for (const Vec2& point : target.normalizedPolygon) {
			if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
				point.x < 0.0f || point.x > 1.0f || point.y < 0.0f || point.y > 1.0f) {
				reject("Surface target contains an invalid normalized point: " + target.name);
				break;
			}
		}
	}
	const MeshBounds actualBounds = bounds.valid ? bounds : mesh.bounds;
	if (!actualBounds.valid || !finite(actualBounds.min) || !finite(actualBounds.max) ||
		actualBounds.min.x > actualBounds.max.x || actualBounds.min.y > actualBounds.max.y ||
		actualBounds.min.z > actualBounds.max.z)
		reject("Asset bounds are invalid.");
	if (!finite(anchor.scale) ||
		anchor.scale.x <= 0.0f ||
		anchor.scale.y <= 0.0f ||
		anchor.scale.z <= 0.0f) {

		reject(
			"Particle anchor scale must be finite and positive."
		);
	}

	// ---------------------------------------------------------
	// Optional native scalar-volume validation.
	// ---------------------------------------------------------
	if (volumetricSource.available) {

		if (volumetricSource.file.empty()) {
			reject(
				"Volumetric source is marked available but has no file."
			);
		}

		if (upper(volumetricSource.format) != "FLOAT32_SDF") {
			reject(
				"Unsupported volumetric source format: " +
				volumetricSource.format
			);
		}

		if (volumetricSource.dimensions[0] == 0u ||
			volumetricSource.dimensions[1] == 0u ||
			volumetricSource.dimensions[2] == 0u) {

			reject(
				"Volumetric source dimensions must be non-zero."
			);
		}

		if (!finite(volumetricSource.isoValue)) {
			reject(
				"Volumetric source iso value must be finite."
			);
		}

		const std::size_t expectedSamples =
			volumetricSource.expectedSampleCount();

		// Manifest-only parsing may temporarily have no samples.
		// Once samples exist, their count must be exact.
		if (!volumetricSource.samples.empty() &&
			volumetricSource.samples.size() != expectedSamples) {

			reject(
				"Volumetric source sample count does not match dimensions."
			);
		}

		for (float sample : volumetricSource.samples) {
			if (!finite(sample)) {
				reject(
					"Volumetric source contains a non-finite sample."
				);
				break;
			}
		}
	}

	return ok;
}

void StaticParticleAsset::refreshDerivedData() {
	mesh.bounds = calculateMeshBounds(mesh);
	bounds = mesh.bounds;
	if (submeshes.empty() && !mesh.indices.empty()) {
		submeshes.push_back({ "Default", 0u,
			static_cast<std::uint32_t>(mesh.indices.size()), 0u });
	}
	if (materials.empty()) materials.emplace_back();
	if (collision.radius <= 0.0f && bounds.valid) collision.radius = bounds.radius;
	if (collision.center.x == 0.0f && collision.center.y == 0.0f &&
		collision.center.z == 0.0f && bounds.valid) collision.center = bounds.center;
	anchor.collision = collision;
}

const TextureResource* StaticParticleAsset::findTexture(const std::string& textureId) const {
	for (const TextureResource& texture : textures) if (texture.id == textureId) return &texture;
	return nullptr;
}
TextureResource* StaticParticleAsset::findTexture(const std::string& textureId) {
	for (TextureResource& texture : textures) if (texture.id == textureId) return &texture;
	return nullptr;
}

const char* textureUsageName(TextureUsage usage) {
	switch (usage) {
	case TextureUsage::BaseColor: return "BASE_COLOR";
	case TextureUsage::AlphaMask: return "ALPHA_MASK";
	case TextureUsage::Height: return "HEIGHT";
	case TextureUsage::Normal: return "NORMAL";
	case TextureUsage::MetallicRoughness: return "METALLIC_ROUGHNESS";
	case TextureUsage::Occlusion: return "OCCLUSION";
	case TextureUsage::Emissive: return "EMISSIVE";
	case TextureUsage::LegacyReflection: return "LEGACY_REFLECTION";
	default: return "CUSTOM";
	}
}

TextureUsage textureUsageFromName(const std::string& name) {
	const std::string value = upper(name);
	if (value == "BASE_COLOR" || value == "BASECOLOR") return TextureUsage::BaseColor;
	if (value == "ALPHA" || value == "ALPHA_MASK") return TextureUsage::AlphaMask;
	if (value == "HEIGHT" || value == "BUMP") return TextureUsage::Height;
	if (value == "NORMAL") return TextureUsage::Normal;
	if (value == "METALLIC_ROUGHNESS") return TextureUsage::MetallicRoughness;
	if (value == "OCCLUSION") return TextureUsage::Occlusion;
	if (value == "EMISSIVE") return TextureUsage::Emissive;
	if (value == "LEGACY_REFLECTION" || value == "CUBEMAP") return TextureUsage::LegacyReflection;
	return TextureUsage::Custom;
}

} // namespace vitru
