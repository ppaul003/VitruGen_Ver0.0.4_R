#ifndef VITRUGEN_ASSET_PATH_RESOLVER_H
#define VITRUGEN_ASSET_PATH_RESOLVER_H

#include <filesystem>
#include <string>
#include <vector>

namespace vitru {

struct AssetPathResolution {
	bool found = false;
	bool ambiguous = false;
	std::filesystem::path resolvedPath;
	std::string assetRelativePath;
	std::vector<std::filesystem::path> candidates;
	std::vector<std::string> warnings;
};

AssetPathResolution resolveAssetPath(
	const std::string& pathAsWritten,
	const std::filesystem::path& meshFile,
	const std::filesystem::path& materialFile,
	const std::filesystem::path& assetRoot);

std::string portableRelativePath(
	const std::filesystem::path& path,
	const std::filesystem::path& assetRoot);

std::string sanitizeAssetName(const std::string& displayName);

} // namespace vitru

#endif
