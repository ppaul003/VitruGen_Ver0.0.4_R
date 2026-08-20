#include "ObjMtlImporter.h"

#include "AssetPathResolver.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace vitru {
namespace fs = std::filesystem;
namespace {

std::string trim(const std::string& input) {
	std::size_t first = 0, last = input.size();
	while (first < last && std::isspace(static_cast<unsigned char>(input[first]))) ++first;
	while (last > first && std::isspace(static_cast<unsigned char>(input[last - 1u]))) --last;
	return input.substr(first, last - first);
}
std::string lower(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return value;
}
std::string stableId(const std::string& prefix, const std::string& value) {
	std::string result = prefix;
	for (unsigned char c : value) result.push_back(std::isalnum(c) ? static_cast<char>(std::tolower(c)) : '_');
	while (!result.empty() && result.back() == '_') result.pop_back();
	return result;
}
bool parseVec3(std::istringstream& stream, Vec3& output) { return static_cast<bool>(stream >> output.x >> output.y >> output.z); }
bool parseInt(const std::string& text, int& output) {
	if (text.empty()) { output = 0; return true; }
	char* end = nullptr; const long value = std::strtol(text.c_str(), &end, 10);
	if (!end || *end != '\0' || value < INT_MIN || value > INT_MAX) return false;
	output = static_cast<int>(value); return true;
}
int resolveIndex(int value, std::size_t count) {
	if (value > 0) return value <= static_cast<int>(count) ? value - 1 : -1;
	if (value < 0) { const int index = static_cast<int>(count) + value; return index >= 0 ? index : -1; }
	return -1;
}
struct FaceToken { int position = 0; int uv = 0; int normal = 0; bool hasUv = false; bool hasNormal = false; };
bool parseFaceToken(const std::string& token, FaceToken& output) {
	const std::size_t first = token.find('/');
	if (first == std::string::npos) return parseInt(token, output.position);
	if (!parseInt(token.substr(0, first), output.position)) return false;
	const std::size_t second = token.find('/', first + 1u);
	if (second == std::string::npos) {
		const std::string uv = token.substr(first + 1u); output.hasUv = !uv.empty(); return parseInt(uv, output.uv);
	}
	const std::string uv = token.substr(first + 1u, second - first - 1u);
	const std::string normal = token.substr(second + 1u);
	output.hasUv = !uv.empty(); output.hasNormal = !normal.empty();
	return parseInt(uv, output.uv) && parseInt(normal, output.normal);
}
Vec3 subtract(const Vec3& a, const Vec3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
Vec3 cross(const Vec3& a, const Vec3& b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
float lengthSquared(const Vec3& v) { return v.x * v.x + v.y * v.y + v.z * v.z; }
void normalize(Vec3& value) { const float length = std::sqrt(lengthSquared(value)); if (length > 1.0e-12f) { value.x /= length; value.y /= length; value.z /= length; } else value = { 0.0f, 1.0f, 0.0f }; }

fs::path findMaterialFile(const std::string& written, const fs::path& obj, const fs::path& root) {
	AssetPathResolution resolved = resolveAssetPath(written, obj, {}, root);
	if (resolved.found) return resolved.resolvedPath;
	const fs::path filename = fs::path(written).filename();
	std::error_code error;
	for (const fs::path& directory : { root / "materials", root }) {
		if (!fs::is_directory(directory, error)) continue;
		for (const fs::directory_entry& entry : fs::recursive_directory_iterator(directory, error)) {
			if (error) break;
			if (entry.is_regular_file(error) && lower(entry.path().filename().string()) == lower(filename.string())) return entry.path();
		}
	}
	return {};
}

std::string textureIdFor(const std::string& material, TextureUsage usage, const std::string& relativePath) {
	return stableId("tex_", material + "_" + textureUsageName(usage) + "_" + fs::path(relativePath).stem().string());
}

void assignTexture(StaticParticleAsset& asset, MaterialSlot& material, TextureUsage usage,
	const std::string& written, const fs::path& obj, const fs::path& mtl, const fs::path& root,
	ObjImportReport& report) {
	AssetPathResolution resolved = resolveAssetPath(written, obj, mtl, root);
	for (const std::string& warning : resolved.warnings) report.warnings.push_back(warning);
	const std::string relative = resolved.found ? resolved.assetRelativePath : fs::path(written).filename().generic_string();
	const std::string id = textureIdFor(material.name, usage, relative);
	TextureResource* texture = asset.findTexture(id);
	if (!texture) {
		TextureResource resource;
		resource.id = id; resource.relativePath = relative; resource.usage = usage;
		resource.colorSpace = (usage == TextureUsage::BaseColor || usage == TextureUsage::Emissive)
			? TextureColorSpace::SRGB : TextureColorSpace::Linear;
		resource.sourcePath = resolved.found ? resolved.resolvedPath.string() : written;
		resource.valid = resolved.found; resource.renderingDeferred = usage != TextureUsage::BaseColor && usage != TextureUsage::AlphaMask;
		asset.textures.push_back(std::move(resource));
		texture = &asset.textures.back();
	}
	switch (usage) {
	case TextureUsage::BaseColor: material.baseColorTextureId = id; break;
	case TextureUsage::AlphaMask: material.alphaTextureId = id; material.alphaMode = AlphaMode::Mask; break;
	case TextureUsage::Height: material.heightTextureId = id; break;
	case TextureUsage::Normal: material.normalTextureId = id; break;
	case TextureUsage::Emissive: material.emissiveTextureId = id; break;
	default: material.customTextureIds.push_back(id); break;
	}
}

bool parseMtl(const fs::path& path, const fs::path& obj, const fs::path& root,
	StaticParticleAsset& asset, std::unordered_map<std::string, std::uint32_t>& materialLookup,
	ObjImportReport& report) {
	std::ifstream input(path); if (!input) { report.warnings.push_back("Material library could not be opened: " + path.string()); return false; }
	MaterialSlot* current = nullptr; bool opacityDefined = false;
	std::string line;
	while (std::getline(input, line)) {
		const std::size_t comment = line.find('#'); if (comment != std::string::npos) line.resize(comment);
		std::istringstream stream(line); std::string tag; if (!(stream >> tag)) continue;
		std::string remainder; std::getline(stream, remainder); remainder = trim(remainder);
		const std::string tagLower = lower(tag);
		if (tagLower == "newmtl") {
			MaterialSlot material; material.name = remainder.empty() ? "Unnamed" : remainder; material.workflow = MaterialWorkflow::LegacyBlinn;
			materialLookup[material.name] = static_cast<std::uint32_t>(asset.materials.size()); asset.materials.push_back(std::move(material)); current = &asset.materials.back(); opacityDefined = false; continue;
		}
		if (!current) { report.warnings.push_back("MTL field before newmtl ignored: " + tag); continue; }
		std::istringstream values(remainder);
		if (tagLower == "ka") values >> current->ambientFactor[0] >> current->ambientFactor[1] >> current->ambientFactor[2];
		else if (tagLower == "kd") values >> current->baseColorFactor[0] >> current->baseColorFactor[1] >> current->baseColorFactor[2];
		else if (tagLower == "ks") values >> current->specularFactor[0] >> current->specularFactor[1] >> current->specularFactor[2];
		else if (tagLower == "ke") values >> current->emissiveFactor[0] >> current->emissiveFactor[1] >> current->emissiveFactor[2];
		else if (tagLower == "ns") values >> current->shininess;
		else if (tagLower == "ni") values >> current->opticalDensity;
		else if (tagLower == "illum") values >> current->legacyIlluminationModel;
		else if (tagLower == "d") { values >> current->baseColorFactor[3]; opacityDefined = true; if (current->baseColorFactor[3] < 0.999f) current->alphaMode = AlphaMode::Blend; }
		else if (tagLower == "tr") { float transparency = 0.0f; values >> transparency; if (!opacityDefined) { current->baseColorFactor[3] = 1.0f - transparency; if (current->baseColorFactor[3] < 0.999f) current->alphaMode = AlphaMode::Blend; } else if (std::fabs((1.0f - transparency) - current->baseColorFactor[3]) > 0.01f) report.warnings.push_back("MTL d/Tr semantics conflict for material " + current->name + "; d takes precedence."); }
		else if (tagLower == "map_kd") assignTexture(asset, *current, TextureUsage::BaseColor, remainder, obj, path, root, report);
		else if (tagLower == "map_d") { assignTexture(asset, *current, TextureUsage::AlphaMask, remainder, obj, path, root, report); report.warnings.push_back("Alpha map semantics require visual review for material " + current->name + "."); }
		else if (tagLower == "map_bump" || tagLower == "bump") assignTexture(asset, *current, TextureUsage::Height, remainder, obj, path, root, report);
		else if (tagLower == "map_ka") assignTexture(asset, *current, TextureUsage::LegacyReflection, remainder, obj, path, root, report);
		else if (tagLower == "map_ke") assignTexture(asset, *current, TextureUsage::Emissive, remainder, obj, path, root, report);
		else if (tagLower != "tf") current->sourceMetadata.push_back(tag + (remainder.empty() ? "" : " " + remainder));
	}
	return true;
}

struct VertexKey {
	int position = -1, uv = -1, normal = -1; std::uint32_t material = 0;
	bool operator==(const VertexKey& other) const { return position == other.position && uv == other.uv && normal == other.normal && material == other.material; }
};
struct VertexKeyHash {
	std::size_t operator()(const VertexKey& key) const {
		std::size_t h = std::hash<int>{}(key.position);
		h ^= std::hash<int>{}(key.uv) + 0x9e3779b9u + (h << 6) + (h >> 2);
		h ^= std::hash<int>{}(key.normal) + 0x9e3779b9u + (h << 6) + (h >> 2);
		h ^= std::hash<std::uint32_t>{}(key.material) + 0x9e3779b9u + (h << 6) + (h >> 2);
		return h;
	}
};

} // namespace

bool importObjStaticParticle(const fs::path& objPath, const fs::path& assetRoot,
	StaticParticleAsset& output, ObjImportReport& report) {
	report = ObjImportReport{}; report.objPath = objPath;
	std::ifstream input(objPath); if (!input) { report.errors.push_back("OBJ could not be opened: " + objPath.string()); return false; }
	StaticParticleAsset asset; asset.name = objPath.stem().string(); asset.source.kind = "EXTERNAL_OBJ";
	asset.source.originalFile = objPath.string(); asset.source.geometryFile = portableRelativePath(objPath, assetRoot); asset.source.assetRoot = assetRoot.string();
	std::vector<Vec3> positions, normals; std::vector<Vec2> uvs;
	std::unordered_map<std::string, std::uint32_t> materials;
	MaterialSlot fallback; fallback.name = "Default Material"; asset.materials.push_back(fallback); materials[fallback.name] = 0u;
	std::uint32_t activeMaterial = 0u; std::string objectName = objPath.stem().string(), groupName;
	std::unordered_map<VertexKey, std::uint32_t, VertexKeyHash> renderVertices;
	std::vector<bool> normalAuthored;
	bool anyUv = false, missingUv = false, anyMissingNormal = false;
	std::string line;
	while (std::getline(input, line)) {
		const std::size_t comment = line.find('#'); if (comment != std::string::npos) line.resize(comment);
		std::istringstream stream(line); std::string tag; if (!(stream >> tag)) continue;
		std::string remainder; std::getline(stream, remainder); remainder = trim(remainder);
		if (tag == "v") { std::istringstream values(remainder); Vec3 value; if (parseVec3(values, value)) positions.push_back(value); else report.warnings.push_back("Malformed OBJ position ignored."); }
		else if (tag == "vt") { std::istringstream values(remainder); Vec2 value; if (values >> value.x >> value.y) uvs.push_back(value); else report.warnings.push_back("Malformed OBJ texcoord ignored."); }
		else if (tag == "vn") { std::istringstream values(remainder); Vec3 value; if (parseVec3(values, value)) { normalize(value); normals.push_back(value); } else report.warnings.push_back("Malformed OBJ normal ignored."); }
		else if (tag == "o") objectName = remainder.empty() ? objectName : remainder;
		else if (tag == "g") groupName = remainder;
		else if (tag == "mtllib") {
			const fs::path mtl = findMaterialFile(remainder, objPath, assetRoot);
			if (mtl.empty()) report.warnings.push_back("OBJ material library is missing: " + remainder);
			else { report.mtlPath = mtl; asset.source.materialFile = portableRelativePath(mtl, assetRoot); parseMtl(mtl, objPath, assetRoot, asset, materials, report); }
		}
		else if (tag == "usemtl") {
			auto found = materials.find(remainder);
			if (found == materials.end()) { MaterialSlot material; material.name = remainder.empty() ? "Unnamed" : remainder; activeMaterial = static_cast<std::uint32_t>(asset.materials.size()); materials[material.name] = activeMaterial; asset.materials.push_back(std::move(material)); report.warnings.push_back("OBJ uses material absent from MTL: " + remainder); }
			else activeMaterial = found->second;
		}
		else if (tag == "f") {
			std::istringstream values(remainder); std::vector<FaceToken> polygon; std::string token; bool faceValid = true;
			while (values >> token) { FaceToken parsed; if (!parseFaceToken(token, parsed)) { faceValid = false; break; } polygon.push_back(parsed); }
			if (!faceValid || polygon.size() < 3u) { report.warnings.push_back("Malformed OBJ face ignored."); continue; }
			for (std::size_t fan = 1u; fan + 1u < polygon.size(); ++fan) {
				const FaceToken triangle[3]{ polygon[0], polygon[fan], polygon[fan + 1u] };
				std::uint32_t triangleIndices[3]{}; bool triangleValid = true;
				for (int corner = 0; corner < 3; ++corner) {
					const int p = resolveIndex(triangle[corner].position, positions.size());
					const int t = triangle[corner].hasUv ? resolveIndex(triangle[corner].uv, uvs.size()) : -1;
					const int n = triangle[corner].hasNormal ? resolveIndex(triangle[corner].normal, normals.size()) : -1;
					if (p < 0 || (triangle[corner].hasUv && t < 0) || (triangle[corner].hasNormal && n < 0)) { triangleValid = false; break; }
					anyUv = anyUv || t >= 0; missingUv = missingUv || t < 0; anyMissingNormal = anyMissingNormal || n < 0;
					const VertexKey key{ p, t, n, activeMaterial }; auto found = renderVertices.find(key);
					if (found == renderVertices.end()) {
						const std::uint32_t index = static_cast<std::uint32_t>(asset.mesh.positions.size());
						asset.mesh.positions.push_back(positions[static_cast<std::size_t>(p)]);
						asset.mesh.uvs.push_back(t >= 0 ? uvs[static_cast<std::size_t>(t)] : Vec2{});
						asset.mesh.normals.push_back(n >= 0 ? normals[static_cast<std::size_t>(n)] : Vec3{});
						normalAuthored.push_back(n >= 0); found = renderVertices.emplace(key, index).first;
					}
					triangleIndices[corner] = found->second;
				}
				if (!triangleValid) { report.warnings.push_back("OBJ face contains an out-of-range index and was ignored."); continue; }
				const std::uint32_t first = static_cast<std::uint32_t>(asset.mesh.indices.size());
				asset.mesh.indices.insert(asset.mesh.indices.end(), triangleIndices, triangleIndices + 3);
				const std::string rangeName = groupName.empty() ? objectName : objectName + "/" + groupName;
				if (!asset.submeshes.empty() && asset.submeshes.back().materialIndex == activeMaterial && asset.submeshes.back().name == rangeName && asset.submeshes.back().firstIndex + asset.submeshes.back().indexCount == first) asset.submeshes.back().indexCount += 3u;
				else asset.submeshes.push_back({ rangeName, first, 3u, activeMaterial });
			}
		}
	}
	if (!anyUv) asset.mesh.uvs.clear();
	else if (missingUv) report.warnings.push_back("OBJ has partially missing UVs; missing tuples use (0,0) and require unwrap review.");
	if (anyMissingNormal) {
		std::vector<Vec3> accumulated(asset.mesh.positions.size());
		for (std::size_t i = 0; i + 2u < asset.mesh.indices.size(); i += 3u) {
			const std::uint32_t a = asset.mesh.indices[i], b = asset.mesh.indices[i + 1u], c = asset.mesh.indices[i + 2u];
			const Vec3 face = cross(subtract(asset.mesh.positions[b], asset.mesh.positions[a]), subtract(asset.mesh.positions[c], asset.mesh.positions[a]));
			for (std::uint32_t index : { a, b, c }) if (!normalAuthored[index]) { accumulated[index].x += face.x; accumulated[index].y += face.y; accumulated[index].z += face.z; }
		}
		for (std::size_t i = 0; i < asset.mesh.normals.size(); ++i) if (!normalAuthored[i]) { normalize(accumulated[i]); asset.mesh.normals[i] = accumulated[i]; }
	}
	asset.refreshDerivedData();
	if (asset.mesh.empty()) { report.errors.push_back("OBJ contains no valid triangles."); return false; }
	std::vector<std::string> errors; if (!asset.validate(&errors)) { report.errors.insert(report.errors.end(), errors.begin(), errors.end()); return false; }
	output = std::move(asset);
	report.sourcePositions = positions.size(); report.sourceTexcoords = uvs.size(); report.sourceNormals = normals.size();
	report.renderVertices = output.mesh.positions.size(); report.triangles = output.mesh.triangleCount(); report.materialRanges = output.submeshes.size();
	report.authoredUvsPreserved = anyUv; report.generatedNormals = anyMissingNormal; report.success = true;
	return true;
}

} // namespace vitru
