#ifndef VITRUGEN_OBJ_MTL_IMPORTER_H
#define VITRUGEN_OBJ_MTL_IMPORTER_H

#include "StaticParticleAsset.h"

#include <filesystem>
#include <string>
#include <vector>

namespace vitru {

struct ObjImportReport {
	bool success = false;
	std::filesystem::path objPath;
	std::filesystem::path mtlPath;
	std::size_t sourcePositions = 0;
	std::size_t sourceTexcoords = 0;
	std::size_t sourceNormals = 0;
	std::size_t renderVertices = 0;
	std::size_t triangles = 0;
	std::size_t materialRanges = 0;
	bool authoredUvsPreserved = false;
	bool generatedNormals = false;
	std::vector<std::string> warnings;
	std::vector<std::string> errors;
};

bool importObjStaticParticle(
	const std::filesystem::path& objPath,
	const std::filesystem::path& assetRoot,
	StaticParticleAsset& output,
	ObjImportReport& report);

} // namespace vitru

#endif
