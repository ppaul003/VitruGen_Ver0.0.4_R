#include "VspaManifest.h"

#include "AssetPathResolver.h"
#include "JsonValue.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace vitru {
namespace fs = std::filesystem;
namespace {

const JsonValue* member(const JsonValue* value, const char* name) { return value ? value->find(name) : nullptr; }
std::string text(const JsonValue* value, const std::string& fallback = {}) { return value && value->isString() ? value->string() : fallback; }
double number(const JsonValue* value, double fallback = 0.0) { return value && value->isNumber() ? value->number() : fallback; }
bool boolean(const JsonValue* value, bool fallback = false) { return value && value->isBoolean() ? value->boolean() : fallback; }
std::string upper(std::string value) { std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); }); return value; }
std::string normalizedFilename(const fs::path& value) {
	std::string result;
	for (unsigned char c : value.filename().string())
		if (std::isalnum(c)) result.push_back(static_cast<char>(std::toupper(c)));
	return result;
}

bool readFile(const fs::path& path, std::string& output) {
	std::ifstream input(path, std::ios::binary); if (!input) return false;
	std::ostringstream data; data << input.rdbuf(); output = data.str(); return input.good() || input.eof();
}
bool writeFile(const fs::path& path, const std::string& data) {
	std::ofstream output(path, std::ios::binary | std::ios::trunc); if (!output) return false;
	output.write(data.data(), static_cast<std::streamsize>(data.size())); return output.good();
}
Vec3 readVec3(const JsonValue* value, const Vec3& fallback = {}) {
	if (!value || !value->isArray() || value->arrayItems().size() < 3u) return fallback;
	return { static_cast<float>(number(&value->arrayItems()[0], fallback.x)), static_cast<float>(number(&value->arrayItems()[1], fallback.y)), static_cast<float>(number(&value->arrayItems()[2], fallback.z)) };
}
void readVec4(const JsonValue* value, float output[4], const float fallback[4]) {
	for (int i = 0; i < 4; ++i) output[i] = fallback[i];
	if (!value || !value->isArray()) return;
	for (std::size_t i = 0; i < 4u && i < value->arrayItems().size(); ++i) output[i] = static_cast<float>(number(&value->arrayItems()[i], output[i]));
}
JsonValue vec3(const Vec3& value) { JsonValue result = JsonValue::array(); result.push(JsonValue(value.x)); result.push(JsonValue(value.y)); result.push(JsonValue(value.z)); return result; }
JsonValue vec4(const float value[4]) { JsonValue result = JsonValue::array(); for (int i = 0; i < 4; ++i) result.push(JsonValue(value[i])); return result; }
JsonValue vec3f(const float value[3]) { JsonValue result = JsonValue::array(); for (int i = 0; i < 3; ++i) result.push(JsonValue(value[i])); return result; }

fs::path resolveBundleFile(const fs::path& root, const std::string& stored, VspaLoadReport& report, bool required) {
	if (stored.empty()) return {};
	std::error_code error; fs::path candidate = root / fs::path(stored);
	if (fs::is_regular_file(candidate, error)) { report.filesFound.push_back(candidate); return candidate; }
	const std::string wanted = upper(fs::path(stored).filename().string());
	const std::string normalizedWanted = normalizedFilename(fs::path(stored));
	std::vector<fs::path> matches;
	if (fs::is_directory(root, error)) for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root, error)) {
		if (error) break;
		if (entry.is_regular_file(error) &&
			(upper(entry.path().filename().string()) == wanted ||
				normalizedFilename(entry.path()) == normalizedWanted))
			matches.push_back(entry.path());
	}
	if (!matches.empty()) {
		std::sort(matches.begin(), matches.end());
		if (matches.size() > 1u) report.warnings.push_back("Ambiguous bundle file; first deterministic match used: " + stored);
		report.filesFound.push_back(matches.front()); return matches.front();
	}
	if (required) report.filesMissing.push_back(stored);
	else report.warnings.push_back("Optional bundle file is missing: " + stored);
	return {};
}

TextureColorSpace colorSpace(const std::string& value, TextureUsage usage) {
	if (upper(value) == "SRGB") return TextureColorSpace::SRGB;
	return (usage == TextureUsage::BaseColor || usage == TextureUsage::Emissive) ? TextureColorSpace::SRGB : TextureColorSpace::Linear;
}
MaterialWorkflow workflow(const std::string& value) { return upper(value).find("PBR") != std::string::npos ? MaterialWorkflow::PBRMetallicRoughness : MaterialWorkflow::LegacyBlinn; }
AlphaMode alphaMode(const std::string& value) { const std::string v = upper(value); return v == "MASK" ? AlphaMode::Mask : v == "BLEND" ? AlphaMode::Blend : AlphaMode::Opaque; }
ParticlePivotMode pivotMode(const std::string& value) { const std::string v = upper(value); return v == "GROUND_CENTER" ? ParticlePivotMode::GroundCenter : v == "AUTHORED" ? ParticlePivotMode::Authored : ParticlePivotMode::BoundsCenter; }
ParticleFitMode fitMode(const std::string& value) { const std::string v = upper(value); return v == "COLLISION_SAFE" ? ParticleFitMode::CollisionSafe : v == "FILL" ? ParticleFitMode::Fill : ParticleFitMode::Preserve; }

const char* workflowName(MaterialWorkflow value) { return value == MaterialWorkflow::PBRMetallicRoughness ? "PBR_METALLIC_ROUGHNESS" : "LEGACY_BLINN"; }
const char* alphaModeName(AlphaMode value) { return value == AlphaMode::Mask ? "MASK" : value == AlphaMode::Blend ? "BLEND" : "OPAQUE"; }
const char* pivotName(ParticlePivotMode value) { return value == ParticlePivotMode::GroundCenter ? "GROUND_CENTER" : value == ParticlePivotMode::Authored ? "AUTHORED" : "BOUNDS_CENTER"; }
const char* fitName(ParticleFitMode value) { return value == ParticleFitMode::CollisionSafe ? "COLLISION_SAFE" : value == ParticleFitMode::Fill ? "FILL" : "PRESERVE"; }

void addManifestTexture(StaticParticleAsset& asset, const std::string& id, const std::string& file,
	TextureUsage usage, const std::string& color, const fs::path& root, VspaLoadReport& report) {
	if (file.empty()) return;
	TextureResource resource; resource.id = id.empty() ? "texture_" + std::to_string(asset.textures.size()) : id;
	resource.relativePath = fs::path(file).generic_string(); resource.usage = usage; resource.colorSpace = colorSpace(color, usage);
	resource.type = TextureType::Texture2D;
	const fs::path resolved = resolveBundleFile(root, file, report, false);
	resource.sourcePath = resolved.empty() ? file : resolved.string(); resource.valid = !resolved.empty();
	resource.renderingDeferred = usage != TextureUsage::BaseColor && usage != TextureUsage::AlphaMask;
	for (const TextureResource& existing : asset.textures) if (existing.id == resource.id) return;
	asset.textures.push_back(std::move(resource));
}

} // namespace

bool loadVspaManifest(const fs::path& manifestPath, StaticParticleAsset& output, VspaLoadReport& report) {
	report = VspaLoadReport{}; report.manifestPath = manifestPath; report.assetRoot = manifestPath.parent_path();
	std::string source;
	if (!readFile(manifestPath, source)) { report.errors.push_back("VSPA manifest could not be read: " + manifestPath.string()); return false; }
	report.filesFound.push_back(manifestPath);
	JsonParseResult parsed = parseJson(source);
	if (!parsed.success || !parsed.value.isObject()) { report.errors.push_back("VSPA JSON parse failed at byte " + std::to_string(parsed.errorOffset) + ": " + parsed.error); return false; }
	const std::string schema = text(parsed.value.find("schema"));
	if (schema != "anaheim.vitrugen.static-particle") { report.errors.push_back("Unsupported VSPA schema: " + schema); return false; }
	const std::uint32_t version = static_cast<std::uint32_t>(number(parsed.value.find("schema_version"), 0.0));
	if (version == 0u) { report.errors.push_back("VSPA schema_version is missing or invalid."); return false; }
	if (version > 1u) { report.errors.push_back("VSPA schema version " + std::to_string(version) + " is newer than this runtime supports."); return false; }
	StaticParticleAsset asset; asset.schema = schema; asset.schemaVersion = version;
	const JsonValue* identity = parsed.value.find("static_particle"); if (!identity) identity = parsed.value.find("asset");
	asset.name = text(member(identity, "name"), text(member(identity, "display_name"), manifestPath.stem().stem().string()));
	const std::string idText = text(member(identity, "id"), text(member(identity, "asset_id")));
	if (!idText.empty()) { try { asset.id = static_cast<AssetId>(std::stoull(idText)); } catch (...) { asset.id = INVALID_ASSET_ID; report.warnings.push_back("Non-numeric external asset ID mapped to a repository ID at load time."); } }
	asset.assetRevision = static_cast<std::uint32_t>(number(member(identity, "asset_revision"), number(member(parsed.value.find("compatibility"), "asset_revision"), 1.0)));
	const JsonValue* geometry = parsed.value.find("geometry");
	asset.source.geometryFile = text(member(geometry, "file")); asset.source.materialFile = text(member(geometry, "material_library"));
	const fs::path geometryPath = resolveBundleFile(report.assetRoot, asset.source.geometryFile, report, true);
	const fs::path materialPath = resolveBundleFile(report.assetRoot, asset.source.materialFile, report, false);
	if (!geometryPath.empty()) asset.source.geometryFile = portableRelativePath(geometryPath, report.assetRoot);
	if (!materialPath.empty()) asset.source.materialFile = portableRelativePath(materialPath, report.assetRoot);
	asset.source.assetRoot = report.assetRoot.string(); asset.source.manifestFile = manifestPath.filename().string();
	const JsonValue* sourceInfo = member(identity, "source"); if (!sourceInfo) sourceInfo = parsed.value.find("source");
	asset.source.kind = text(member(sourceInfo, "kind")); asset.source.originalFile = text(member(sourceInfo, "original_file")); asset.source.licenseNote = text(member(sourceInfo, "license_note"));
	const JsonValue* coordinate = parsed.value.find("coordinate_system"); asset.source.sourceUnits = text(member(coordinate, "units")); asset.source.sourceUpAxis = text(member(coordinate, "up_axis"));

	const JsonValue* materials = parsed.value.find("materials");
	if (materials && materials->isArray()) for (const JsonValue& item : materials->arrayItems()) {
		MaterialSlot material; material.name = text(item.find("name"), text(item.find("slot"), "Material"));
		material.workflow = workflow(text(item.find("workflow"))); const float white[4]{ 1,1,1,1 }; readVec4(item.find("base_color_factor"), material.baseColorFactor, white);
		material.metallicFactor = static_cast<float>(number(item.find("metallic_factor"), 0.0)); material.roughnessFactor = static_cast<float>(number(item.find("roughness_factor"), 1.0));
		const Vec3 emissive = readVec3(item.find("emissive_factor")); material.emissiveFactor[0] = emissive.x; material.emissiveFactor[1] = emissive.y; material.emissiveFactor[2] = emissive.z; material.emissiveIntensity = static_cast<float>(number(item.find("emissive_intensity"), 1.0));
		material.alphaMode = alphaMode(text(item.find("alpha_mode"))); material.alphaCutoff = static_cast<float>(number(item.find("alpha_cutoff"), 0.5)); material.doubleSided = boolean(item.find("double_sided"));
		material.castShadows = boolean(item.find("cast_shadows"), true); material.receiveShadows = boolean(item.find("receive_shadows"), true); material.rayTracingVisible = boolean(item.find("ray_tracing_visible"), true);
		material.baseColorTextureId = text(item.find("base_color_texture_id")); material.alphaTextureId = text(item.find("alpha_texture_id")); material.heightTextureId = text(item.find("height_texture_id")); material.normalTextureId = text(item.find("normal_texture_id")); material.emissiveTextureId = text(item.find("emissive_texture_id"));
		const JsonValue* legacyBase = item.find("base_color_texture"); if (legacyBase && legacyBase->isObject()) { const std::string id = "tex_" + std::to_string(asset.textures.size()) + "_base"; addManifestTexture(asset, id, text(legacyBase->find("file")), TextureUsage::BaseColor, text(legacyBase->find("color_space")), report.assetRoot, report); material.baseColorTextureId = id; }
		const JsonValue* legacyNormal = item.find("normal_texture"); if (legacyNormal && legacyNormal->isObject()) { const std::string id = "tex_" + std::to_string(asset.textures.size()) + "_normal"; addManifestTexture(asset, id, text(legacyNormal->find("file")), TextureUsage::Normal, text(legacyNormal->find("color_space")), report.assetRoot, report); material.normalTextureId = id; }
		const JsonValue* legacyEmissive = item.find("emissive_texture"); if (legacyEmissive && legacyEmissive->isObject()) { const std::string id = "tex_" + std::to_string(asset.textures.size()) + "_emissive"; addManifestTexture(asset, id, text(legacyEmissive->find("file")), TextureUsage::Emissive, text(legacyEmissive->find("color_space")), report.assetRoot, report); material.emissiveTextureId = id; }
		asset.materials.push_back(std::move(material));
	}
	const JsonValue* textures = parsed.value.find("textures");
	if (textures && textures->isArray()) for (const JsonValue& item : textures->arrayItems()) addManifestTexture(asset, text(item.find("id")), text(item.find("file")), textureUsageFromName(text(item.find("usage"))), text(item.find("color_space")), report.assetRoot, report);
	const JsonValue* surfaceTargets = parsed.value.find("surface_targets");
	if (surfaceTargets && surfaceTargets->isArray()) for (const JsonValue& item : surfaceTargets->arrayItems()) {
		SurfaceTarget target;
		target.name = text(item.find("name"));
		target.faceIndex = static_cast<std::uint32_t>(number(item.find("face_index"), 4.0));
		const JsonValue* points = item.find("normalized_points");
		if (points && points->isArray()) for (const JsonValue& point : points->arrayItems()) {
			if (!point.isArray() || point.arrayItems().size() < 2u) continue;
			target.normalizedPolygon.push_back({
				static_cast<float>(number(&point.arrayItems()[0])),
				static_cast<float>(number(&point.arrayItems()[1]))
			});
		}
		if (!target.name.empty() && target.normalizedPolygon.size() >= 3u)
			asset.surfaceTargets.push_back(std::move(target));
	}
	const JsonValue* environments = parsed.value.find("environment_maps");
	if (environments && environments->isArray()) for (const JsonValue& item : environments->arrayItems()) {
		const std::string id = text(item.find("id"));
		addManifestTexture(asset, id, text(item.find("file")), textureUsageFromName(text(item.find("usage"))), text(item.find("color_space")), report.assetRoot, report);
		TextureResource* resource = asset.findTexture(id);
		if (resource) { resource->type = TextureType::Cubemap; resource->renderingDeferred = true; }
	}
	const JsonValue* submeshes = parsed.value.find("submeshes");
	if (submeshes && submeshes->isArray()) for (const JsonValue& item : submeshes->arrayItems()) asset.submeshes.push_back({ text(item.find("name")), static_cast<std::uint32_t>(number(item.find("first_index"))), static_cast<std::uint32_t>(number(item.find("index_count"))), static_cast<std::uint32_t>(number(item.find("material_index"))) });
	const JsonValue* anchor = parsed.value.find("particle_anchor");
	asset.anchor.particleIndex = static_cast<std::uint32_t>(number(member(anchor, "particle_index"), 0.0)); asset.anchor.pivotMode = pivotMode(text(member(anchor, "pivot_mode"))); asset.anchor.fitMode = fitMode(text(member(anchor, "fit_mode")));
	asset.anchor.translation = readVec3(member(anchor, "translation"), readVec3(member(anchor, "local_translation"))); asset.anchor.rotationDegrees = readVec3(member(anchor, "rotation_degrees"), readVec3(member(anchor, "local_rotation_degrees"))); asset.anchor.scale = readVec3(member(anchor, "scale"), readVec3(member(anchor, "local_scale"), { 1,1,1 }));
	asset.collision.radius = static_cast<float>(number(member(anchor, "radius"), 0.5)); asset.anchor.collision = asset.collision;
	const JsonValue* volumetric =
		parsed.value.find("volumetric_source");

	asset.volumetricSource.available =
		boolean(
			member(volumetric, "available"),
			false
		);

	asset.volumetricSource.file =
		text(
			member(volumetric, "file")
		);

	asset.volumetricSource.format =
		text(
			member(volumetric, "format"),
			"FLOAT32_SDF"
		);

	asset.volumetricSource.isoValue =
		static_cast<float>(
			number(
				member(volumetric, "iso_value"),
				0.0
			)
			);

	const JsonValue* volumeDimensions =
		member(
			volumetric,
			"dimensions"
		);

	if (volumeDimensions &&
		volumeDimensions->isArray() &&
		volumeDimensions->arrayItems().size() >= 3u) {

		asset.volumetricSource.dimensions[0] =
			static_cast<std::uint32_t>(
				number(
					&volumeDimensions->arrayItems()[0],
					0.0
				)
				);

		asset.volumetricSource.dimensions[1] =
			static_cast<std::uint32_t>(
				number(
					&volumeDimensions->arrayItems()[1],
					0.0
				)
				);

		asset.volumetricSource.dimensions[2] =
			static_cast<std::uint32_t>(
				number(
					&volumeDimensions->arrayItems()[2],
					0.0
				)
				);
	}

	// A manifest that claims an available native volume must
	// reference a real bundle file.
	if (asset.volumetricSource.available) {

		if (asset.volumetricSource.file.empty()) {

			report.errors.push_back(
				"VSPA volumetric source is marked available "
				"but has no file."
			);
		}
		else {

			const fs::path volumePath =
				resolveBundleFile(
					report.assetRoot,
					asset.volumetricSource.file,
					report,
					true
				);

			if (volumePath.empty()) {

				report.errors.push_back(
					"VSPA volumetric source file is missing: " +
					asset.volumetricSource.file
				);
			}
			else {

				asset.volumetricSource.file =
					portableRelativePath(
						volumePath,
						report.assetRoot
					);
			}
		}
	}

	output = std::move(asset);

	if (geometryPath.empty()) {
		report.errors.push_back(
			"VSPA required geometry is missing."
		);
	}

	report.success =
		report.errors.empty();

	return report.success;
}

bool writeVspaManifest(const fs::path& manifestPath, const StaticParticleAsset& asset, VspaSaveReport& report) {
	report = VspaSaveReport{}; report.manifestPath = manifestPath; report.assetRoot = manifestPath.parent_path();
	JsonValue root = JsonValue::object(); root["schema"] = JsonValue("anaheim.vitrugen.static-particle"); root["schema_version"] = JsonValue(static_cast<double>(asset.schemaVersion));
	JsonValue identity = JsonValue::object(); identity["asset_revision"] = JsonValue(static_cast<double>(asset.assetRevision)); identity["id"] = JsonValue(std::to_string(asset.id)); identity["name"] = JsonValue(asset.name); identity["type"] = JsonValue("STATIC_PARTICLE"); root["static_particle"] = identity;
	JsonValue geometry = JsonValue::object(); geometry["file"] = JsonValue(fs::path(asset.source.geometryFile).generic_string()); geometry["format"] = JsonValue("OBJ"); geometry["material_library"] = JsonValue(fs::path(asset.source.materialFile).generic_string()); geometry["index_policy"] = JsonValue("POSITION_UV_NORMAL_MATERIAL"); geometry["texcoord_0"] = JsonValue(!asset.mesh.uvs.empty()); geometry["texcoord_1"] = JsonValue(!asset.mesh.uvs1.empty()); root["geometry"] = geometry;
	JsonValue bounds = JsonValue::object(); bounds["minimum"] = vec3(asset.bounds.min); bounds["maximum"] = vec3(asset.bounds.max); bounds["center"] = vec3(asset.bounds.center); bounds["extent"] = vec3(asset.bounds.extent); bounds["radius"] = JsonValue(asset.bounds.radius); root["bounds"] = bounds;
	JsonValue submeshes = JsonValue::array(); for (const SubMesh& submesh : asset.submeshes) { JsonValue item = JsonValue::object(); item["name"] = JsonValue(submesh.name); item["first_index"] = JsonValue(static_cast<double>(submesh.firstIndex)); item["index_count"] = JsonValue(static_cast<double>(submesh.indexCount)); item["material_index"] = JsonValue(static_cast<double>(submesh.materialIndex)); submeshes.push(std::move(item)); } root["submeshes"] = submeshes;
	JsonValue materials = JsonValue::array(); for (const MaterialSlot& material : asset.materials) { JsonValue item = JsonValue::object(); item["name"] = JsonValue(material.name); item["workflow"] = JsonValue(workflowName(material.workflow)); item["base_color_factor"] = vec4(material.baseColorFactor); item["metallic_factor"] = JsonValue(material.metallicFactor); item["roughness_factor"] = JsonValue(material.roughnessFactor); item["emissive_factor"] = vec3f(material.emissiveFactor); item["emissive_intensity"] = JsonValue(material.emissiveIntensity); item["alpha_mode"] = JsonValue(alphaModeName(material.alphaMode)); item["alpha_cutoff"] = JsonValue(material.alphaCutoff); item["double_sided"] = JsonValue(material.doubleSided); item["base_color_texture_id"] = JsonValue(material.baseColorTextureId); item["alpha_texture_id"] = JsonValue(material.alphaTextureId); item["height_texture_id"] = JsonValue(material.heightTextureId); item["normal_texture_id"] = JsonValue(material.normalTextureId); item["metallic_roughness_texture_id"] = JsonValue(material.metallicRoughnessTextureId); item["occlusion_texture_id"] = JsonValue(material.occlusionTextureId); item["emissive_texture_id"] = JsonValue(material.emissiveTextureId); item["cast_shadows"] = JsonValue(material.castShadows); item["receive_shadows"] = JsonValue(material.receiveShadows); item["ray_tracing_visible"] = JsonValue(material.rayTracingVisible); materials.push(std::move(item)); } root["materials"] = materials;
	JsonValue textures = JsonValue::array(), environments = JsonValue::array(); for (const TextureResource& texture : asset.textures) { JsonValue item = JsonValue::object(); item["id"] = JsonValue(texture.id); item["file"] = JsonValue(fs::path(texture.relativePath).generic_string()); item["type"] = JsonValue(texture.type == TextureType::Cubemap ? "CUBEMAP" : "TEXTURE_2D"); item["usage"] = JsonValue(textureUsageName(texture.usage)); item["color_space"] = JsonValue(texture.colorSpace == TextureColorSpace::SRGB ? "SRGB" : "LINEAR"); item["rendering_support"] = JsonValue(texture.renderingDeferred ? "DEFERRED" : "ACTIVE"); if (texture.type == TextureType::Cubemap) environments.push(item); else textures.push(item); } root["textures"] = textures; root["environment_maps"] = environments;
	JsonValue targets = JsonValue::array(); for (const SurfaceTarget& target : asset.surfaceTargets) { JsonValue item = JsonValue::object(); item["name"] = JsonValue(target.name); item["face_index"] = JsonValue(static_cast<double>(target.faceIndex)); JsonValue points = JsonValue::array(); for (const Vec2& point : target.normalizedPolygon) { JsonValue pair = JsonValue::array(); pair.push(JsonValue(point.x)); pair.push(JsonValue(point.y)); points.push(std::move(pair)); } item["normalized_points"] = points; targets.push(std::move(item)); } root["surface_targets"] = targets;
	JsonValue anchor = JsonValue::object(); anchor["particle_index"] = JsonValue(static_cast<double>(asset.anchor.particleIndex)); anchor["pivot_mode"] = JsonValue(pivotName(asset.anchor.pivotMode)); anchor["fit_mode"] = JsonValue(fitName(asset.anchor.fitMode)); anchor["translation"] = vec3(asset.anchor.translation); anchor["rotation_degrees"] = vec3(asset.anchor.rotationDegrees); anchor["scale"] = vec3(asset.anchor.scale); anchor["collision_radius"] = JsonValue(asset.collision.radius); root["particle_anchor"] = anchor;
	JsonValue source = JsonValue::object(); source["kind"] = JsonValue(asset.source.kind); source["original_file"] = JsonValue(fs::path(asset.source.originalFile).filename().generic_string()); source["license_note"] = JsonValue(asset.source.licenseNote); source["source_units"] = JsonValue(asset.source.sourceUnits); source["source_up_axis"] = JsonValue(asset.source.sourceUpAxis); root["source"] = source;
	JsonValue workspace = JsonValue::object(); workspace["file"] = JsonValue("SINGLE_PARTICLE_DATA/p0.obj"); workspace["canonical_asset_file"] = JsonValue(false); root["workspace_register"] = workspace;
	JsonValue compatibility = JsonValue::object(); compatibility["minimum_vitrugen_version"] = JsonValue("0.0.4"); compatibility["asset_revision"] = JsonValue(static_cast<double>(asset.assetRevision)); root["compatibility"] = compatibility;
	JsonValue volume =
		JsonValue::object();

	volume["available"] =
		JsonValue(
			asset.volumetricSource.available
		);

	volume["file"] =
		JsonValue(
			fs::path(
				asset.volumetricSource.file
			).generic_string()
		);

	volume["format"] =
		JsonValue(
			asset.volumetricSource.format
		);

	JsonValue volumeDimensions =
		JsonValue::array();

	volumeDimensions.push(
		JsonValue(
			static_cast<double>(
				asset.volumetricSource.dimensions[0]
				)
		)
	);

	volumeDimensions.push(
		JsonValue(
			static_cast<double>(
				asset.volumetricSource.dimensions[1]
				)
		)
	);

	volumeDimensions.push(
		JsonValue(
			static_cast<double>(
				asset.volumetricSource.dimensions[2]
				)
		)
	);

	volume["dimensions"] =
		std::move(volumeDimensions);

	volume["iso_value"] =
		JsonValue(
			asset.volumetricSource.isoValue
		);

	root["volumetric_source"] =
		std::move(volume);
	if (!writeFile(manifestPath, writeJson(root, 2))) { report.errors.push_back("VSPA manifest could not be written: " + manifestPath.string()); return false; }
	report.filesWritten.push_back(manifestPath); report.success = true; return true;
}

} // namespace vitru
