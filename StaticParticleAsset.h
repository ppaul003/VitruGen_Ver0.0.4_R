#ifndef VITRUGEN_STATIC_PARTICLE_ASSET_H
#define VITRUGEN_STATIC_PARTICLE_ASSET_H

#include "MeshGeometry.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vitru {

using AssetId = std::uint64_t;
static constexpr AssetId INVALID_ASSET_ID = 0;

enum class TextureUsage {
	BaseColor = 0,
	AlphaMask,
	Height,
	Normal,
	MetallicRoughness,
	Occlusion,
	Emissive,
	Custom,
	LegacyReflection
};

enum class TextureType { Texture2D = 0, Cubemap };
enum class TextureColorSpace { Linear = 0, SRGB };
enum class MaterialWorkflow { LegacyBlinn = 0, PBRMetallicRoughness };
enum class AlphaMode { Opaque = 0, Mask, Blend };
enum class ParticlePivotMode { BoundsCenter = 0, GroundCenter, Authored };
enum class ParticleFitMode { Preserve = 0, CollisionSafe, Fill };

struct TextureResource {
	std::string id;
	std::string relativePath;
	TextureType type = TextureType::Texture2D;
	TextureUsage usage = TextureUsage::Custom;
	TextureColorSpace colorSpace = TextureColorSpace::Linear;
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	std::uint32_t channels = 0;
	std::vector<std::uint8_t> pixels;
	// Non-owning runtime diagnostic. EuclidRenderer owns and deletes GL handles.
	std::uint32_t runtimeGpuHandle = 0;
	bool loaded = false;
	bool valid = false;
	bool renderingDeferred = false;
	std::string sourcePath;
	std::string sourceMetadata;
};

struct MaterialSlot {
	std::string name = "Default Material";
	MaterialWorkflow workflow = MaterialWorkflow::LegacyBlinn;
	float baseColorFactor[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
	float metallicFactor = 0.0f;
	float roughnessFactor = 1.0f;
	float emissiveFactor[3]{ 0.0f, 0.0f, 0.0f };
	// TEXTURE_MAP_2D authored multiplier. Ke remains the color factor;
	// this scalar preserves the editor's independent 0.0 - 4.0 control.
	float emissiveIntensity = 1.0f;
	float ambientFactor[3]{ 0.0f, 0.0f, 0.0f };
	float specularFactor[3]{ 0.0f, 0.0f, 0.0f };
	float shininess = 0.0f;
	float opticalDensity = 1.0f;
	int legacyIlluminationModel = 2;
	AlphaMode alphaMode = AlphaMode::Opaque;
	float alphaCutoff = 0.5f;
	bool doubleSided = false;
	std::string baseColorTextureId;
	std::string alphaTextureId;
	std::string heightTextureId;
	std::string normalTextureId;
	std::string metallicRoughnessTextureId;
	std::string occlusionTextureId;
	std::string emissiveTextureId;
	std::vector<std::string> customTextureIds;
	bool castShadows = true;
	bool receiveShadows = true;
	bool rayTracingVisible = true;
	std::vector<std::string> sourceMetadata;
};

// A named selection mask authored against one cell of the canonical
// BOX_ATLAS UV layout. Points are normalized to the selected face so
// the target is independent of the saved texture resolution.
struct SurfaceTarget {
	std::string name;
	std::uint32_t faceIndex = 4u;
	std::vector<Vec2> normalizedPolygon;
};

struct SubMesh {
	std::string name;
	std::uint32_t firstIndex = 0;
	std::uint32_t indexCount = 0;
	std::uint32_t materialIndex = 0;
};

struct CollisionProxy {
	enum class Shape { Sphere = 0, Box, Capsule, Cone };
	Shape shape = Shape::Sphere;
	Vec3 center{};
	Vec3 halfExtents{ 0.5f, 0.5f, 0.5f };
	float radius = 0.5f;
	float height = 1.0f;
};

struct ParticleAnchor {
	ParticlePivotMode pivotMode = ParticlePivotMode::GroundCenter;
	ParticleFitMode fitMode = ParticleFitMode::CollisionSafe;
	Vec3 translation{};
	Vec3 rotationDegrees{};
	Vec3 scale{ 1.0f, 1.0f, 1.0f };
	std::uint32_t particleIndex = 0;
	CollisionProxy collision;
};

struct SourceProvenance {
	std::string kind;
	std::string originalFile;
	std::string manifestFile;
	std::string assetRoot;
	std::string geometryFile;
	std::string materialFile;
	std::string licenseNote;
	std::string sourceUnits;
	std::string sourceUpAxis;
	float sourceMeterScale = 1.0f;
	std::vector<std::string> metadata;
};

struct VolumetricSourceMetadata {
	bool available = false;

	// Portable path relative to the VSPA bundle root.
	//
	// Example:
	//     volume/base_volume.f32
	std::string file;

	// Sprint A0 supports one canonical native format:
	//
	//     FLOAT32_SDF
	//
	// Each sample is one IEEE-754 32-bit float.
	std::string format = "FLOAT32_SDF";

	// X, Y, Z scalar-field dimensions.
	std::array<std::uint32_t, 3> dimensions{
		{ 0u, 0u, 0u }
	};

	float isoValue = 0.0f;

	// CPU-side payload used during save/load.
	//
	// The VSPA JSON does not serialize this vector. It is written
	// separately into file as raw FLOAT32_SDF data.
	std::vector<float> samples;

	std::size_t expectedSampleCount() const {
		return
			static_cast<std::size_t>(dimensions[0]) *
			static_cast<std::size_t>(dimensions[1]) *
			static_cast<std::size_t>(dimensions[2]);
	}
};

struct StaticParticleAsset {
	AssetId id = INVALID_ASSET_ID;
	std::string name;
	MeshGeometry mesh;
	MeshBounds bounds;
	CollisionProxy collision;
	ParticleAnchor anchor;
	std::vector<SubMesh> submeshes;
	std::vector<MaterialSlot> materials;
	std::vector<TextureResource> textures;
	std::vector<SurfaceTarget> surfaceTargets;
	SourceProvenance source;
	std::string schema = "anaheim.vitrugen.static-particle";
	std::uint32_t schemaVersion = 1;
	std::uint32_t assetRevision = 1;
	VolumetricSourceMetadata volumetricSource;

	bool validate(std::vector<std::string>* errors = nullptr) const;
	void refreshDerivedData();
	const TextureResource* findTexture(const std::string& textureId) const;
	TextureResource* findTexture(const std::string& textureId);
};

MeshBounds calculateMeshBounds(const MeshGeometry& mesh);
const char* textureUsageName(TextureUsage usage);
TextureUsage textureUsageFromName(const std::string& name);

} // namespace vitru

#endif
