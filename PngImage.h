#ifndef VITRUGEN_PNG_IMAGE_H
#define VITRUGEN_PNG_IMAGE_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace vitru {

struct ImageRGBA8 {
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	std::vector<std::uint8_t> pixels;
	bool valid() const {
		return width > 0 && height > 0 &&
			pixels.size() == static_cast<std::size_t>(width) * height * 4u;
	}
};

bool loadPngImage(
	const std::filesystem::path& path,
	ImageRGBA8& output,
	std::string* error = nullptr,
	bool flipVerticallyForOpenGL = true);

bool writePngImage(
	const std::filesystem::path& path,
	const ImageRGBA8& image,
	std::string* error = nullptr);

ImageRGBA8 makeSolidImage(
	std::uint32_t width,
	std::uint32_t height,
	std::uint8_t r,
	std::uint8_t g,
	std::uint8_t b,
	std::uint8_t a = 255u);

} // namespace vitru

#endif
