#ifndef VITRUGEN_VSPA_MANIFEST_H
#define VITRUGEN_VSPA_MANIFEST_H

#include "StaticParticleAsset.h"

#include <filesystem>
#include <string>
#include <vector>

namespace vitru {

struct VspaLoadReport {
	bool success = false;
	std::filesystem::path manifestPath;
	std::filesystem::path assetRoot;
	std::vector<std::filesystem::path> filesFound;
	std::vector<std::string> filesMissing;
	std::vector<std::string> warnings;
	std::vector<std::string> errors;
};

struct VspaSaveReport {
	bool success = false;
	std::filesystem::path manifestPath;
	std::filesystem::path assetRoot;
	std::vector<std::filesystem::path> filesWritten;
	std::vector<std::string> warnings;
	std::vector<std::string> errors;
};

bool loadVspaManifest(
	const std::filesystem::path& manifestPath,
	StaticParticleAsset& output,
	VspaLoadReport& report);

bool writeVspaManifest(
	const std::filesystem::path& manifestPath,
	const StaticParticleAsset& asset,
	VspaSaveReport& report);

} // namespace vitru

#endif
