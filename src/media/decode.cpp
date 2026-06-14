#include "media/decode.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb_image.h"

namespace metaagent::media {
namespace {

std::filesystem::path to_native_path(const core::String& path)
{
	return std::filesystem::path(path);
}

bool fill_metadata(const core::String& path, RgbaImage& image)
{
	std::error_code error;
	const std::filesystem::path native_path = to_native_path(path);
	if (!std::filesystem::exists(native_path, error))
	{
		return false;
	}

	image.metadata.source_path = path;
	image.metadata.file_size = static_cast<int64_t>(std::filesystem::file_size(native_path, error));
	if (error)
	{
		image.metadata.file_size = 0;
	}

	const auto modified_time = std::filesystem::last_write_time(native_path, error);
	if (error)
	{
		image.metadata.file_modified_unix = 0;
		return true;
	}

	const auto file_clock_now = std::filesystem::file_time_type::clock::now();
	const auto system_now = std::chrono::system_clock::now();
	const auto modified_since_epoch = modified_time.time_since_epoch();
	const auto file_now_since_epoch = file_clock_now.time_since_epoch();
	const auto system_now_since_epoch = system_now.time_since_epoch();
	const auto modified_system = system_now + (modified_since_epoch - file_now_since_epoch);
	image.metadata.file_modified_unix = std::chrono::duration_cast<std::chrono::seconds>(
		modified_system.time_since_epoch()).count();
	return true;
}

bool read_file_bytes(const core::String& path, std::vector<uint8_t>& out_bytes)
{
	std::ifstream stream(to_native_path(path), std::ios::binary);
	if (!stream)
	{
		return false;
	}

	stream.seekg(0, std::ios::end);
	const std::streamoff size = stream.tellg();
	if (size <= 0)
	{
		return false;
	}

	stream.seekg(0, std::ios::beg);
	out_bytes.resize(static_cast<size_t>(size));
	stream.read(reinterpret_cast<char*>(out_bytes.data()), size);
	return stream.good();
}

bool decode_stbi_pixels(
	stbi_uc* pixels,
	const int width,
	const int height,
	const int channels,
	RgbaImage& out_image,
	const core::String& source_label)
{
	if (!pixels || width <= 0 || height <= 0 || channels < 1)
	{
		if (pixels)
		{
			stbi_image_free(pixels);
		}
		return false;
	}

	out_image.width = width;
	out_image.height = height;
	out_image.metadata.width = width;
	out_image.metadata.height = height;
	out_image.metadata.channel_count = channels;
	out_image.metadata.source_path = source_label;
	out_image.pixels.resize(static_cast<size_t>(width * height));

	const size_t pixel_count = static_cast<size_t>(width * height);
	for (size_t index = 0; index < pixel_count; ++index)
	{
		const size_t offset = index * static_cast<size_t>(channels);
		core::ColorRGBA color;
		color.r = pixels[offset + 0];
		color.g = channels > 1 ? pixels[offset + 1] : pixels[offset + 0];
		color.b = channels > 2 ? pixels[offset + 2] : pixels[offset + 0];
		color.a = channels > 3 ? pixels[offset + 3] : 255;
		out_image.pixels[index] = color;
	}

	stbi_image_free(pixels);
	return true;
}

} // namespace

bool get_file_identity(
	const core::String& path,
	int64_t& out_file_size,
	int64_t& out_modified_unix)
{
	out_file_size = 0;
	out_modified_unix = 0;

	RgbaImage probe;
	if (!fill_metadata(path, probe))
	{
		return false;
	}

	out_file_size = probe.metadata.file_size;
	out_modified_unix = probe.metadata.file_modified_unix;
	return true;
}

bool decode_image_from_file(const core::String& path, RgbaImage& out_image)
{
	out_image = {};

	std::vector<uint8_t> file_bytes;
	if (!read_file_bytes(path, file_bytes))
	{
		return false;
	}

	return decode_image_from_memory(file_bytes.data(), file_bytes.size(), out_image, path)
		&& fill_metadata(path, out_image);
}

bool decode_image_from_memory(
	const uint8_t* data,
	const size_t size,
	RgbaImage& out_image,
	const core::String& source_label)
{
	out_image = {};
	if (!data || size == 0)
	{
		return false;
	}

	int width = 0;
	int height = 0;
	int channels = 0;
	stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4);
	if (!pixels)
	{
		return false;
	}

	return decode_stbi_pixels(pixels, width, height, 4, out_image, source_label);
}

} // namespace metaagent::media
