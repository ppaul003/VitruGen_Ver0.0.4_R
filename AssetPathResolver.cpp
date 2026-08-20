#include "AssetPathResolver.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace vitru {
namespace fs = std::filesystem;
namespace {

std::string lower(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return value;
}

fs::path unquote(const std::string& input) {
	std::string value = input;
	while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
	while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
	if (value.size() >= 2u && ((value.front() == '"' && value.back() == '"') ||
		(value.front() == '\'' && value.back() == '\''))) value = value.substr(1u, value.size() - 2u);
	return fs::path(value);
}

void addIfFile(std::vector<fs::path>& candidates, const fs::path& path) {
	std::error_code error;
	if (path.empty() || !fs::is_regular_file(path, error)) return;
	const fs::path canonical = fs::weakly_canonical(path, error);
	const fs::path value = error ? path.lexically_normal() : canonical;
	for (const fs::path& existing : candidates) if (lower(existing.string()) == lower(value.string())) return;
	candidates.push_back(value);
}

void caseInsensitiveChild(std::vector<fs::path>& candidates, const fs::path& parent, const fs::path& name) {
	std::error_code error;
	if (!fs::is_directory(parent, error)) return;
	const std::string wanted = lower(name.filename().string());
	for (const fs::directory_entry& entry : fs::directory_iterator(parent, error)) {
		if (error) break;
		if (entry.is_regular_file(error) && lower(entry.path().filename().string()) == wanted) addIfFile(candidates, entry.path());
	}
}

} // namespace

std::string portableRelativePath(const fs::path& path, const fs::path& assetRoot) {
	std::error_code error;
	fs::path relative = fs::relative(path, assetRoot, error);
	if (error || relative.empty() || relative.native().find(L"..") == 0u) return path.filename().generic_string();
	return relative.generic_string();
}

AssetPathResolution resolveAssetPath(
	const std::string& pathAsWritten,
	const fs::path& meshFile,
	const fs::path& materialFile,
	const fs::path& assetRoot) {
	AssetPathResolution result;
	const fs::path written = unquote(pathAsWritten);
	const fs::path filename = written.filename();
	std::vector<fs::path> direct;
	addIfFile(direct, written);
	if (!meshFile.empty()) addIfFile(direct, meshFile.parent_path() / written);
	if (!materialFile.empty()) addIfFile(direct, materialFile.parent_path() / written);
	addIfFile(direct, assetRoot / written);
	addIfFile(direct, assetRoot / "textures" / filename);

	// Windows sources frequently contain obsolete absolute paths. Also check
	// filename-only matches beside the mesh/MTL before the recursive fallback.
	if (!meshFile.empty()) caseInsensitiveChild(direct, meshFile.parent_path(), filename);
	if (!materialFile.empty()) caseInsensitiveChild(direct, materialFile.parent_path(), filename);
	caseInsensitiveChild(direct, assetRoot, filename);
	caseInsensitiveChild(direct, assetRoot / "textures", filename);

	if (!direct.empty()) {
		result.candidates = direct;
		result.resolvedPath = direct.front();
		result.found = true;
		result.ambiguous = direct.size() > 1u;
	}
	else if (!filename.empty()) {
		std::error_code error;
		const fs::path textureRoot = assetRoot / "textures";
		if (fs::is_directory(textureRoot, error)) {
			const std::string wanted = lower(filename.string());
			for (const fs::directory_entry& entry : fs::recursive_directory_iterator(textureRoot, error)) {
				if (error) break;
				if (entry.is_regular_file(error) && lower(entry.path().filename().string()) == wanted)
					addIfFile(result.candidates, entry.path());
			}
		}
		if (!result.candidates.empty()) {
			std::sort(result.candidates.begin(), result.candidates.end(), [](const fs::path& a, const fs::path& b) {
				return lower(a.generic_string()) < lower(b.generic_string());
			});
			result.resolvedPath = result.candidates.front();
			result.found = true;
			result.ambiguous = result.candidates.size() > 1u;
		}
	}
	if (result.found) result.assetRelativePath = portableRelativePath(result.resolvedPath, assetRoot);
	if (result.ambiguous) result.warnings.push_back("Ambiguous asset filename; deterministic first match selected: " + filename.string());
	if (!result.found) result.warnings.push_back("Asset file is missing: " + pathAsWritten);
	return result;
}

std::string sanitizeAssetName(const std::string& displayName) {
	std::string result;
	result.reserve(displayName.size());
	bool previousUnderscore = false;
	for (unsigned char c : displayName) {
		const bool accepted = std::isalnum(c) != 0 || c == '-' || c == '_';
		if (accepted) { result.push_back(static_cast<char>(c)); previousUnderscore = false; }
		else if (!previousUnderscore && !result.empty()) { result.push_back('_'); previousUnderscore = true; }
	}
	while (!result.empty() && (result.back() == '_' || result.back() == '.')) result.pop_back();
	if (result.empty()) result = "StaticParticle";
	static const std::set<std::string> reserved = {
		"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
		"lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"
	};
	if (reserved.count(lower(result))) result = "Asset_" + result;
	return result;
}

} // namespace vitru
