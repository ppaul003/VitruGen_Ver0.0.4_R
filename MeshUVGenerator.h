#ifndef VITRUGEN_MESH_UV_GENERATOR_H
#define VITRUGEN_MESH_UV_GENERATOR_H

#include "MeshGeometry.h"

#include <filesystem>
#include <string>

namespace vitru {

struct MeshUVGenerationReport {
	bool success = false;
	std::size_t inputVertices = 0;
	std::size_t outputVertices = 0;
	std::size_t seamDuplicates = 0;
	std::string policy = "BOX_ATLAS_6_DIRECTION";
	std::vector<std::string> errors;
};

MeshUVGenerationReport generateBoxAtlasUVs(
	MeshGeometry& mesh,
	float cellPadding = 0.035f);

bool writeUvGuidePng(
	const MeshGeometry& mesh,
	const std::filesystem::path& path,
	std::uint32_t size = 1024u,
	std::string* error = nullptr);

} // namespace vitru

#endif
