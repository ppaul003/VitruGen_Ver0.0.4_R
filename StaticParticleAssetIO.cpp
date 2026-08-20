#include "StaticParticleAssetIO.h"

#include "AssetPathResolver.h"
#include "MeshUVGenerator.h"
#include "ObjMtlImporter.h"
#include "PngImage.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

namespace vitru {
namespace fs = std::filesystem;
namespace {

void setError(std::string* error, const std::string& value) { if (error) *error = value; }
std::string forward(const fs::path& path) { return path.generic_string(); }
std::string lowerText(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return value;
}

bool safeGeneratedPath(const fs::path& path, const fs::path& root) {
	std::error_code error;
	const fs::path parent = fs::weakly_canonical(path.parent_path(), error);
	const fs::path canonicalRoot = fs::weakly_canonical(root, error);
	if (error || parent.empty() || canonicalRoot.empty()) return false;
	const std::string p = parent.generic_string(), r = canonicalRoot.generic_string();
	return p.size() >= r.size() && std::equal(r.begin(), r.end(), p.begin(), [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
}

bool writeReport(const fs::path& path, const StaticParticleAsset& asset, const std::vector<std::string>& warnings) {
	std::ofstream output(path, std::ios::trunc); if (!output) return false;
	output << "VITRUGEN STATIC PARTICLE ASSET SAVE REPORT\n\n";
	output << "Asset: " << asset.name << "\n";
	output << "Vertices: " << asset.mesh.positions.size() << "\n";
	output << "Triangles: " << asset.mesh.triangleCount() << "\n";
	output << "Materials: " << asset.materials.size() << "\n";
	output << "Textures: " << asset.textures.size() << "\n";
	output << "TEXCOORD_0: " << (asset.mesh.uvs.size() == asset.mesh.positions.size() ? "YES" : "NO") << "\n";
	output << "Volumetric source available: " << (asset.volumetricSource.available ? "YES" : "NO") << "\n";
	if (!warnings.empty()) { output << "\nWarnings:\n"; for (const std::string& warning : warnings) output << "- " << warning << "\n"; }
	return output.good();
}

bool writeFloat32Volume(
	const fs::path& path,
	const VolumetricSourceMetadata& volume,
	std::string* error) {

	if (!volume.available) {
		setError(
			error,
			"Cannot write an unavailable volumetric source."
		);
		return false;
	}

	const std::size_t expectedSamples =
		volume.expectedSampleCount();

	if (expectedSamples == 0u) {
		setError(
			error,
			"Volumetric source dimensions are invalid."
		);
		return false;
	}

	if (volume.samples.size() != expectedSamples) {
		setError(
			error,
			"Volumetric source sample count does not match dimensions."
		);
		return false;
	}

	std::ofstream output(
		path,
		std::ios::binary |
		std::ios::trunc
	);

	if (!output) {
		setError(
			error,
			"Could not open volume file for writing: " +
			path.string()
		);
		return false;
	}

	const std::size_t byteCount =
		volume.samples.size() *
		sizeof(float);

	output.write(
		reinterpret_cast<const char*>(
			volume.samples.data()
			),
		static_cast<std::streamsize>(
			byteCount
			)
	);

	if (!output.good()) {
		setError(
			error,
			"Volume file write failed: " +
			path.string()
		);
		return false;
	}

	return true;
}

bool readFloat32Volume(
	const fs::path& path,
	VolumetricSourceMetadata& volume,
	std::string* error) {

	if (lowerText(volume.format) != "float32_sdf") {
		setError(
			error,
			"Unsupported volume format: " +
			volume.format
		);
		return false;
	}

	const std::size_t expectedSamples =
		volume.expectedSampleCount();

	if (expectedSamples == 0u) {
		setError(
			error,
			"Volume dimensions are missing or invalid."
		);
		return false;
	}

	const std::size_t expectedBytes =
		expectedSamples *
		sizeof(float);

	std::ifstream input(
		path,
		std::ios::binary |
		std::ios::ate
	);

	if (!input) {
		setError(
			error,
			"Could not open volume file: " +
			path.string()
		);
		return false;
	}

	const std::streamoff fileBytes =
		input.tellg();

	if (fileBytes !=
		static_cast<std::streamoff>(
			expectedBytes
			)) {

		setError(
			error,
			"Volume file byte count does not match "
			"the declared dimensions."
		);
		return false;
	}

	input.seekg(
		0,
		std::ios::beg
	);

	volume.samples.assign(
		expectedSamples,
		0.0f
	);

	input.read(
		reinterpret_cast<char*>(
			volume.samples.data()
			),
		static_cast<std::streamsize>(
			expectedBytes
			)
	);

	if (!input.good()) {
		setError(
			error,
			"Volume file read failed: " +
			path.string()
		);
		volume.samples.clear();
		return false;
	}

	for (float sample : volume.samples) {
		if (!std::isfinite(sample)) {
			setError(
				error,
				"Volume file contains a non-finite sample."
			);
			volume.samples.clear();
			return false;
		}
	}

	return true;
}

const char* alphaName(AlphaMode mode) { return mode == AlphaMode::Mask ? "MASK" : mode == AlphaMode::Blend ? "BLEND" : "OPAQUE"; }

void mergeManifestMetadata(const StaticParticleAsset& manifest, StaticParticleAsset& imported) {
	imported.name = manifest.name;
	imported.id = manifest.id;
	imported.schema = manifest.schema;
	imported.schemaVersion = manifest.schemaVersion;
	imported.assetRevision = manifest.assetRevision;
	imported.anchor = manifest.anchor;
	imported.collision = manifest.collision;
	imported.volumetricSource = manifest.volumetricSource;
	imported.surfaceTargets = manifest.surfaceTargets;
	SourceProvenance source = imported.source;
	source.manifestFile = manifest.source.manifestFile;
	source.assetRoot = manifest.source.assetRoot;
	source.licenseNote = manifest.source.licenseNote;
	source.sourceUnits = manifest.source.sourceUnits;
	source.sourceUpAxis = manifest.source.sourceUpAxis;
	imported.source = std::move(source);
	for (const TextureResource& texture : manifest.textures) {
		if (!imported.findTexture(texture.id)) imported.textures.push_back(texture);
	}
	// Preserve extended material metadata when a manifest material has the same
	// name as an authored MTL slot. The MTL geometry ranges remain authoritative.
	for (MaterialSlot& material : imported.materials) {
		for (const MaterialSlot& saved : manifest.materials) if (saved.name == material.name) {
			const std::string base = material.baseColorTextureId;
			const std::string alpha = material.alphaTextureId;
			const std::string height = material.heightTextureId;
			material = saved;
			if (!base.empty()) material.baseColorTextureId = base;
			if (!alpha.empty()) material.alphaTextureId = alpha;
			if (!height.empty()) material.heightTextureId = height;
			break;
		}
	}
}

fs::path textureSourcePath(const TextureResource& texture, const fs::path& root) {
	std::error_code error;
	if (!texture.sourcePath.empty() && fs::is_regular_file(fs::path(texture.sourcePath), error)) return fs::path(texture.sourcePath);
	const fs::path relative = root / fs::path(texture.relativePath);
	if (fs::is_regular_file(relative, error)) return relative;
	return {};
}

} // namespace

bool writeStaticParticleObj(const StaticParticleAsset& asset, const fs::path& objPath,
	const std::string& materialReference, std::string* error) {
	if (asset.mesh.empty() || asset.mesh.normals.size() != asset.mesh.positions.size()) {
		setError(error, "OBJ writer requires a non-empty indexed mesh with one normal per vertex."); return false;
	}
	const bool hasUvs = asset.mesh.uvs.size() == asset.mesh.positions.size();
	std::ofstream output(objPath, std::ios::trunc); if (!output) { setError(error, "Could not open OBJ for writing: " + objPath.string()); return false; }
	output << "# VitruGen Static Particle Asset indexed OBJ\n";
	output << "# vertices " << asset.mesh.positions.size() << " triangles " << asset.mesh.triangleCount() << "\n";
	if (!materialReference.empty()) output << "mtllib " << forward(fs::path(materialReference)) << "\n";
	output << "o SP_MCAD_MESH\n" << std::fixed << std::setprecision(7);
	for (const Vec3& p : asset.mesh.positions) output << "v " << p.x << ' ' << p.y << ' ' << p.z << '\n';
	if (hasUvs) for (const Vec2& uv : asset.mesh.uvs) output << "vt " << uv.x << ' ' << uv.y << '\n';
	for (const Vec3& normal : asset.mesh.normals) output << "vn " << normal.x << ' ' << normal.y << ' ' << normal.z << '\n';
	auto face = [&](std::size_t offset) {
		const std::uint32_t a = asset.mesh.indices[offset] + 1u, b = asset.mesh.indices[offset + 1u] + 1u, c = asset.mesh.indices[offset + 2u] + 1u;
		if (hasUvs) output << "f " << a << '/' << a << '/' << a << ' ' << b << '/' << b << '/' << b << ' ' << c << '/' << c << '/' << c << '\n';
		else output << "f " << a << "//" << a << ' ' << b << "//" << b << ' ' << c << "//" << c << '\n';
	};
	if (asset.submeshes.empty()) {
		if (!asset.materials.empty()) output << "usemtl " << asset.materials.front().name << '\n';
		for (std::size_t i = 0; i + 2u < asset.mesh.indices.size(); i += 3u) face(i);
	}
	else {
		for (const SubMesh& submesh : asset.submeshes) {
			output << "g " << (submesh.name.empty() ? "Mesh" : submesh.name) << '\n';
			if (submesh.materialIndex < asset.materials.size()) output << "usemtl " << asset.materials[submesh.materialIndex].name << '\n';
			const std::size_t end = (std::min)(asset.mesh.indices.size(), static_cast<std::size_t>(submesh.firstIndex) + submesh.indexCount);
			for (std::size_t i = submesh.firstIndex; i + 2u < end; i += 3u) face(i);
		}
	}
	if (!output.good()) { setError(error, "OBJ write failed: " + objPath.string()); return false; }
	return true;
}

bool writeStaticParticleMtl(const StaticParticleAsset& asset, const fs::path& mtlPath, std::string* error) {
	std::ofstream output(mtlPath, std::ios::trunc); if (!output) { setError(error, "Could not open MTL for writing: " + mtlPath.string()); return false; }
	output << "# VitruGen Static Particle Asset materials\n" << std::fixed << std::setprecision(6);
	for (const MaterialSlot& material : asset.materials) {
		output << "\nnewmtl " << material.name << '\n';
		output << "Ka " << material.ambientFactor[0] << ' ' << material.ambientFactor[1] << ' ' << material.ambientFactor[2] << '\n';
		output << "Kd " << material.baseColorFactor[0] << ' ' << material.baseColorFactor[1] << ' ' << material.baseColorFactor[2] << '\n';
		output << "Ks " << material.specularFactor[0] << ' ' << material.specularFactor[1] << ' ' << material.specularFactor[2] << '\n';
		output << "Ke " << material.emissiveFactor[0] << ' ' << material.emissiveFactor[1] << ' ' << material.emissiveFactor[2] << '\n';
		output << "Ns " << material.shininess << "\nNi " << material.opticalDensity << "\nd " << material.baseColorFactor[3] << "\nillum " << material.legacyIlluminationModel << '\n';
		auto map = [&](const char* tag, const std::string& id) { const TextureResource* texture = asset.findTexture(id); if (texture) output << tag << " ../textures/" << fs::path(texture->relativePath).filename().generic_string() << '\n'; };
		map("map_Kd", material.baseColorTextureId); map("map_d", material.alphaTextureId); map("map_bump", material.heightTextureId); map("map_Ke", material.emissiveTextureId);
		output << "# alpha_mode " << alphaName(material.alphaMode) << " cutoff " << material.alphaCutoff << '\n';
	}
	if (!output.good()) { setError(error, "MTL write failed: " + mtlPath.string()); return false; }
	return true;
}

bool loadStaticParticleBundle(const fs::path& manifestPath, StaticParticleAsset& output,
	StaticAssetOperationReport& report, ProjectAssetRepository* repository, const fs::path& workspaceObj) {
	report = StaticAssetOperationReport{}; report.phase = "reading VSPA"; report.manifestPath = manifestPath; report.assetRoot = manifestPath.parent_path();
	StaticParticleAsset manifestAsset; VspaLoadReport manifestReport;
	if (!loadVspaManifest(manifestPath, manifestAsset, manifestReport)) { report.errors = manifestReport.errors; report.warnings = manifestReport.warnings; return false; }
	report.warnings.insert(report.warnings.end(), manifestReport.warnings.begin(), manifestReport.warnings.end());
	fs::path objPath = report.assetRoot / fs::path(manifestAsset.source.geometryFile);
	std::error_code error;
	if (!fs::is_regular_file(objPath, error)) {
		for (const fs::path& file : manifestReport.filesFound) if (file.extension() == ".obj" || file.extension() == ".OBJ") { objPath = file; break; }
	}
	report.phase = "loading OBJ/MTL"; StaticParticleAsset imported; ObjImportReport importReport;
	if (!importObjStaticParticle(objPath, report.assetRoot, imported, importReport)) { report.errors = importReport.errors; report.warnings.insert(report.warnings.end(), importReport.warnings.begin(), importReport.warnings.end()); return false; }
	report.warnings.insert(
		report.warnings.end(),
		importReport.warnings.begin(),
		importReport.warnings.end()
	);

	mergeManifestMetadata(
		manifestAsset,
		imported
	);

	// ---------------------------------------------------------
	// Load the optional native SP_MCAD scalar field.
	//
	// This is CPU-only bundle I/O. Upload to CUDA occurs later,
	// on the EuclidEngine render/main thread.
	// ---------------------------------------------------------
	if (imported.volumetricSource.available) {

		report.phase =
			"loading volumetric source";

		const fs::path volumePath =
			report.assetRoot /
			fs::path(
				imported.volumetricSource.file
			);

		std::string volumeError;

		if (!readFloat32Volume(
			volumePath,
			imported.volumetricSource,
			&volumeError)) {

			report.errors.push_back(
				volumeError
			);

			return false;
		}

		report.filesWritten.push_back(
			volumePath
		);
	}

	// Preserve unassigned local compatibility resources as forward-compatible
	// metadata. This keeps alpha/camo/control masks and cubemap faces in VSPA
	// without pretending the A0 renderer supports those roles.
	{
		std::set<std::string> represented;
		for (const TextureResource& texture : imported.textures) {
			if (!texture.sourcePath.empty()) represented.insert(lowerText(fs::path(texture.sourcePath).filename().string()));
			else represented.insert(lowerText(fs::path(texture.relativePath).filename().string()));
		}
		const fs::path texturesRoot = report.assetRoot / "textures";
		if (fs::is_directory(texturesRoot, error)) {
			for (const fs::directory_entry& entry : fs::recursive_directory_iterator(texturesRoot, error)) {
				if (error || !entry.is_regular_file(error)) continue;
				const std::string extension = lowerText(entry.path().extension().string());
				if (extension != ".png") continue;
				const std::string filename = lowerText(entry.path().filename().string());
				if (!represented.insert(filename).second) continue;
				TextureResource resource;
				resource.id = "extra_" + sanitizeAssetName(entry.path().stem().string());
				resource.relativePath = portableRelativePath(entry.path(), report.assetRoot);
				resource.sourcePath = entry.path().string();
				resource.valid = true;
				const std::string parent = lowerText(entry.path().parent_path().filename().string());
				if (parent.find("cubemap") != std::string::npos) {
					resource.type = TextureType::Cubemap;
					resource.usage = TextureUsage::LegacyReflection;
				}
				else if (filename.find("bump") != std::string::npos) resource.usage = TextureUsage::Height;
				else if (filename.find("alpha") != std::string::npos) resource.usage = TextureUsage::AlphaMask;
				else resource.usage = TextureUsage::Custom;
				resource.colorSpace = TextureColorSpace::Linear;
				resource.renderingDeferred = true;
				imported.textures.push_back(std::move(resource));
			}
		}
	}
	report.phase = "resolving textures";
	for (TextureResource& texture : imported.textures) {
		if (texture.type != TextureType::Texture2D) { texture.renderingDeferred = true; continue; }
		const fs::path sourcePath = textureSourcePath(texture, report.assetRoot);
		if (sourcePath.empty()) { texture.loaded = false; texture.valid = false; report.warnings.push_back("Texture fallback active; file missing: " + texture.relativePath); continue; }
		ImageRGBA8 image; std::string imageError;
		if (!loadPngImage(sourcePath, image, &imageError, true)) { texture.loaded = false; texture.valid = false; report.warnings.push_back("Texture fallback active: " + imageError); continue; }
		texture.width = image.width; texture.height = image.height; texture.channels = 4u; texture.pixels = std::move(image.pixels); texture.loaded = true; texture.valid = true; texture.sourcePath = sourcePath.string();
	}
	imported.refreshDerivedData(); std::vector<std::string> validation;
	if (!imported.validate(&validation)) { report.errors.insert(report.errors.end(), validation.begin(), validation.end()); return false; }
	if (repository) { AssetId id = repository->addStaticParticle(imported); repository->setActiveStaticParticle(id); imported.id = id; }
	if (!workspaceObj.empty()) {
		report.phase = "refreshing p0"; fs::create_directories(workspaceObj.parent_path(), error);
		const fs::path workspaceMtl = workspaceObj.parent_path() / (workspaceObj.stem().string() + ".mtl"); std::string writeError;
		if (!writeStaticParticleMtl(imported, workspaceMtl, &writeError) || !writeStaticParticleObj(imported, workspaceObj, workspaceMtl.filename().generic_string(), &writeError)) { report.errors.push_back("p0.obj refresh failed: " + writeError); return false; }
		report.filesWritten.push_back(workspaceObj); report.filesWritten.push_back(workspaceMtl);
	}
	output = std::move(imported); report.phase = "complete"; report.success = true; return true;
}

bool saveStaticParticleBundle(const StaticParticleAsset& source, const fs::path& outputRoot,
	const std::string& displayName, const fs::path& workspaceObj, StaticAssetOperationReport& report) {
	report = StaticAssetOperationReport{}; report.phase = "validating mesh";
	StaticParticleAsset asset = source; asset.name = displayName.empty() ? source.name : displayName;
	const std::string safeName = sanitizeAssetName(asset.name);
	const fs::path staticRoot = outputRoot / "STATIC_PARTICLES";
	const fs::path destination = staticRoot / safeName;
	const fs::path temporary = staticRoot / (safeName + ".__saving__");
	const fs::path backup = staticRoot / (safeName + ".__previous__");
	report.assetRoot = destination; report.manifestPath = destination / (safeName + ".vspa.json");
	std::error_code error; fs::create_directories(staticRoot, error);
	if (error || !safeGeneratedPath(temporary, staticRoot)) { report.errors.push_back("Output path is unsafe or unavailable."); return false; }
	fs::remove_all(
		temporary,
		error
	);

	error.clear();

	fs::create_directories(
		temporary / "geometry",
		error
	);

	fs::create_directories(
		temporary / "materials",
		error
	);

	fs::create_directories(
		temporary / "textures",
		error
	);

	fs::create_directories(
		temporary / "volume",
		error
	);

	fs::create_directories(
		temporary / "preview",
		error
	);

	fs::create_directories(
		temporary / "reports",
		error
	);
	if (error) { report.errors.push_back("Temporary bundle directory could not be created."); return false; }
	if (asset.mesh.uvs.size() != asset.mesh.positions.size()) { report.phase = "generating UVs"; const MeshUVGenerationReport uv = generateBoxAtlasUVs(asset.mesh); if (!uv.success) { report.errors.insert(report.errors.end(), uv.errors.begin(), uv.errors.end()); fs::remove_all(temporary, error); return false; } }
	asset.refreshDerivedData();
	if (asset.materials.empty()) asset.materials.emplace_back();
	bool hasBaseColor = false; for (const MaterialSlot& material : asset.materials) if (!material.baseColorTextureId.empty() && asset.findTexture(material.baseColorTextureId)) hasBaseColor = true;
	if (!hasBaseColor) {
		TextureResource texture; texture.id = "tex_" + safeName + "_basecolor"; texture.relativePath = "textures/" + safeName + "_basecolor.png"; texture.usage = TextureUsage::BaseColor; texture.colorSpace = TextureColorSpace::SRGB; texture.width = 1024u; texture.height = 1024u; texture.channels = 4u; texture.pixels = makeSolidImage(1024u, 1024u, 190u, 195u, 200u).pixels; texture.loaded = true; texture.valid = true; asset.textures.push_back(std::move(texture)); asset.materials.front().baseColorTextureId = asset.textures.back().id;
	}
	const std::string guideId = "tex_" + safeName + "_uv_guide";
	if (!asset.findTexture(guideId)) { TextureResource guide; guide.id = guideId; guide.relativePath = "textures/" + safeName + "_uv_guide.png"; guide.usage = TextureUsage::Custom; guide.colorSpace = TextureColorSpace::SRGB; guide.renderingDeferred = true; asset.textures.push_back(std::move(guide)); }
	asset.source.geometryFile =
		"geometry/" +
		safeName +
		".obj";

	asset.source.materialFile =
		"materials/" +
		safeName +
		".mtl";

	asset.source.assetRoot =
		destination.string();

	asset.source.manifestFile =
		safeName +
		".vspa.json";

	// ---------------------------------------------------------
	// Normalize native volume output.
	// ---------------------------------------------------------
	if (asset.volumetricSource.available) {

		asset.volumetricSource.file =
			"volume/base_volume.f32";

		asset.volumetricSource.format =
			"FLOAT32_SDF";

		const std::size_t expectedSamples =
			asset.volumetricSource.expectedSampleCount();

		if (expectedSamples == 0u) {

			report.errors.push_back(
				"Native volume dimensions are invalid."
			);

			fs::remove_all(
				temporary,
				error
			);

			return false;
		}

		if (asset.volumetricSource.samples.size() !=
			expectedSamples) {

			report.errors.push_back(
				"Native volume sample count does not match dimensions."
			);

			fs::remove_all(
				temporary,
				error
			);

			return false;
		}
	}
	std::vector<std::string> validation; if (!asset.validate(&validation)) { report.errors.insert(report.errors.end(), validation.begin(), validation.end()); fs::remove_all(temporary, error); return false; }
	const fs::path obj = temporary / asset.source.geometryFile; const fs::path mtl = temporary / asset.source.materialFile; std::string writeError;
	report.phase = "writing OBJ"; if (!writeStaticParticleObj(asset, obj, "../materials/" + safeName + ".mtl", &writeError)) { report.errors.push_back(writeError); fs::remove_all(temporary, error); return false; } report.filesWritten.push_back(obj);
	report.phase = "writing MTL"; if (!writeStaticParticleMtl(asset, mtl, &writeError)) { report.errors.push_back(writeError); fs::remove_all(temporary, error); return false; } report.filesWritten.push_back(mtl);
	report.phase = "writing/copying textures";
	for (TextureResource& texture : asset.textures) {
		const fs::path destinationTexture = temporary / "textures" / fs::path(texture.relativePath).filename(); texture.relativePath = "textures/" + destinationTexture.filename().generic_string();
		if (texture.id == guideId) continue;
		const fs::path sourceTexture = textureSourcePath(texture, fs::path(source.source.assetRoot));
		if (!sourceTexture.empty()) {
			// Preserve imported PNG bytes and their authored row orientation.
			fs::copy_file(sourceTexture, destinationTexture, fs::copy_options::overwrite_existing, error);
			if (error) { report.errors.push_back("Texture copy failed: " + sourceTexture.string()); fs::remove_all(temporary, error); return false; }
		}
		else if (texture.width > 0u && texture.height > 0u && texture.pixels.size() == static_cast<std::size_t>(texture.width) * texture.height * 4u) {
			ImageRGBA8 image{ texture.width, texture.height, texture.pixels };
			if (!writePngImage(destinationTexture, image, &writeError)) { report.errors.push_back(writeError); fs::remove_all(temporary, error); return false; }
		}
		else {
			report.warnings.push_back("Missing texture replaced by white fallback: " + texture.relativePath);
			ImageRGBA8 fallback = makeSolidImage(4u, 4u, 255u, 255u, 255u);
			if (!writePngImage(destinationTexture, fallback, &writeError)) { report.errors.push_back(writeError); fs::remove_all(temporary, error); return false; }
		}
		report.filesWritten.push_back(destinationTexture);
	}

	const fs::path guidePath =
		temporary /
		"textures" /
		(safeName + "_uv_guide.png");

	if (!writeUvGuidePng(
		asset.mesh,
		guidePath,
		1024u,
		&writeError)) {

		report.errors.push_back(
			writeError
		);

		fs::remove_all(
			temporary,
			error
		);

		return false;
	}

	report.filesWritten.push_back(
		guidePath
	);

	// ---------------------------------------------------------
	// Write the native scalar field before the manifest.
	//
	// Temporary-bundle validation will immediately read this file
	// back and verify its dimensions and byte count.
	// ---------------------------------------------------------
	if (asset.volumetricSource.available) {

		report.phase =
			"writing native volume";

		const fs::path volumePath =
			temporary /
			fs::path(
				asset.volumetricSource.file
			);

		if (!writeFloat32Volume(
			volumePath,
			asset.volumetricSource,
			&writeError)) {

			report.errors.push_back(
				writeError
			);

			fs::remove_all(
				temporary,
				error
			);

			return false;
		}

		report.filesWritten.push_back(
			volumePath
		);
	}

	report.phase =
		"writing VSPA";
	
	VspaSaveReport manifestReport; const fs::path tempManifest = temporary / (safeName + ".vspa.json"); if (!writeVspaManifest(tempManifest, asset, manifestReport)) { report.errors = manifestReport.errors; fs::remove_all(temporary, error); return false; } report.filesWritten.push_back(tempManifest);
	const fs::path saveReport = temporary / "reports" / "save_report.txt"; if (!writeReport(saveReport, asset, report.warnings)) { report.errors.push_back("Save report could not be written."); fs::remove_all(temporary, error); return false; } report.filesWritten.push_back(saveReport);
	report.phase = "validating bundle"; StaticParticleAsset validated; StaticAssetOperationReport validationReport; if (!loadStaticParticleBundle(tempManifest, validated, validationReport, nullptr, {})) { report.errors.push_back("Temporary bundle validation failed."); report.errors.insert(report.errors.end(), validationReport.errors.begin(), validationReport.errors.end()); fs::remove_all(temporary, error); return false; }
	if (!workspaceObj.empty()) { fs::create_directories(workspaceObj.parent_path(), error); const fs::path workspaceMtl = workspaceObj.parent_path() / (workspaceObj.stem().string() + ".mtl"); if (!writeStaticParticleMtl(validated, workspaceMtl, &writeError) || !writeStaticParticleObj(validated, workspaceObj, workspaceMtl.filename().generic_string(), &writeError)) { report.errors.push_back("p0.obj refresh failed: " + writeError); fs::remove_all(temporary, error); return false; } report.filesWritten.push_back(workspaceObj); }
	report.phase = "atomic replace"; fs::remove_all(backup, error); error.clear(); const bool hadDestination = fs::exists(destination, error);
	if (hadDestination) { fs::rename(destination, backup, error); if (error) { report.errors.push_back("Existing asset could not be moved for atomic replacement."); fs::remove_all(temporary, error); return false; } }
	fs::rename(temporary, destination, error);
	if (error) { if (hadDestination) { std::error_code restore; fs::rename(backup, destination, restore); } report.errors.push_back("Atomic asset replacement failed; previous bundle was restored."); return false; }
	fs::remove_all(backup, error); report.assetRoot = destination; report.manifestPath = destination / (safeName + ".vspa.json"); report.phase = "complete"; report.success = true; return true;
}

std::vector<StaticAssetCatalogEntry> enumerateStaticParticleAssets(const fs::path& inputsRoot, const fs::path& outputRoot) {
	std::vector<StaticAssetCatalogEntry> result;
	auto scan = [&](const fs::path& root, const char* source) {
		std::error_code error; if (!fs::is_directory(root, error)) return;
		for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root, error)) {
			if (error) break; if (!entry.is_regular_file(error)) continue;
			const std::string filename = entry.path().filename().string();
			if (filename.size() < 10u || filename.substr(filename.size() - 10u) != ".vspa.json") continue;
			StaticParticleAsset asset; VspaLoadReport report; const bool valid = loadVspaManifest(entry.path(), asset, report);
			StaticAssetCatalogEntry item; item.displayName = asset.name.empty() ? entry.path().stem().stem().string() : asset.name; item.source = source; item.manifestPath = entry.path(); item.valid = valid; item.validationMessage = valid ? "VALID" : (report.errors.empty() ? "INVALID" : report.errors.front()); result.push_back(std::move(item));
		}
	};
	scan(inputsRoot, "INPUT"); scan(outputRoot, "OUTPUT");
	std::sort(result.begin(), result.end(), [](const StaticAssetCatalogEntry& a, const StaticAssetCatalogEntry& b) { if (a.displayName != b.displayName) return a.displayName < b.displayName; return a.manifestPath < b.manifestPath; }); return result;
}

} // namespace vitru
