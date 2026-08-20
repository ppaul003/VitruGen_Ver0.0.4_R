#ifndef VITRUGEN_STATIC_PARTICLE_ASSET_IO_H
#define VITRUGEN_STATIC_PARTICLE_ASSET_IO_H

#include "ProjectAssetRepository.h"
#include "VspaManifest.h"

#include <filesystem>
#include <string>
#include <vector>

namespace vitru {

struct StaticAssetOperationReport {
	bool success = false;
	std::string phase;
	std::filesystem::path assetRoot;
	std::filesystem::path manifestPath;
	std::vector<std::filesystem::path> filesWritten;
	std::vector<std::string> warnings;
	std::vector<std::string> errors;
};

struct StaticAssetCatalogEntry {
	std::string displayName;
	std::string source;
	std::filesystem::path manifestPath;
	bool valid = false;
	std::string validationMessage;
};

bool writeStaticParticleObj(
	const StaticParticleAsset& asset,
	const std::filesystem::path& objPath,
	const std::string& materialReference,
	std::string* error = nullptr);

bool writeStaticParticleMtl(
	const StaticParticleAsset& asset,
	const std::filesystem::path& mtlPath,
	std::string* error = nullptr);

bool loadStaticParticleBundle(
	const std::filesystem::path& manifestPath,
	StaticParticleAsset& output,
	StaticAssetOperationReport& report,
	ProjectAssetRepository* repository = nullptr,
	const std::filesystem::path& workspaceObj = {});

bool saveStaticParticleBundle(
	const StaticParticleAsset& source,
	const std::filesystem::path& outputRoot,
	const std::string& displayName,
	const std::filesystem::path& workspaceObj,
	StaticAssetOperationReport& report);

std::vector<StaticAssetCatalogEntry> enumerateStaticParticleAssets(
	const std::filesystem::path& inputsRoot,
	const std::filesystem::path& outputStaticParticlesRoot);

} // namespace vitru

#endif
