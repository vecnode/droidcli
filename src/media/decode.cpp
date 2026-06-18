#include "media/decode.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

extern "C" {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244)
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
#include <libswscale/swscale.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
}

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

struct MemoryInput {
	const uint8_t* data = nullptr;
	size_t size = 0;
	size_t offset = 0;
};

int read_memory_packet(void* opaque, uint8_t* buffer, int buffer_size)
{
	auto* input = static_cast<MemoryInput*>(opaque);
	if (!input || !input->data || buffer_size <= 0)
	{
		return AVERROR_EOF;
	}

	if (input->offset >= input->size)
	{
		return AVERROR_EOF;
	}

	const size_t remaining = input->size - input->offset;
	const size_t to_read = static_cast<size_t>(buffer_size) < remaining
		? static_cast<size_t>(buffer_size)
		: remaining;
	std::memcpy(buffer, input->data + input->offset, to_read);
	input->offset += to_read;
	return static_cast<int>(to_read);
}

int64_t seek_memory_packet(void* opaque, int64_t offset, int whence)
{
	auto* input = static_cast<MemoryInput*>(opaque);
	if (!input)
	{
		return AVERROR(EINVAL);
	}

	switch (whence)
	{
	case SEEK_SET:
		input->offset = offset < 0 ? 0 : static_cast<size_t>(offset);
		break;
	case SEEK_CUR:
		input->offset = static_cast<size_t>(static_cast<int64_t>(input->offset) + offset);
		break;
	case SEEK_END:
		input->offset = static_cast<size_t>(static_cast<int64_t>(input->size) + offset);
		break;
	case AVSEEK_SIZE:
		return static_cast<int64_t>(input->size);
	default:
		return AVERROR(EINVAL);
	}

	if (input->offset > input->size)
	{
		input->offset = input->size;
	}

	return static_cast<int64_t>(input->offset);
}

bool frame_to_rgba(AVFrame* frame, RgbaImage& out_image)
{
	if (!frame || frame->width <= 0 || frame->height <= 0)
	{
		return false;
	}

	SwsContext* scaler = sws_getContext(
		frame->width,
		frame->height,
		static_cast<AVPixelFormat>(frame->format),
		frame->width,
		frame->height,
		AV_PIX_FMT_RGBA,
		SWS_BILINEAR,
		nullptr,
		nullptr,
		nullptr);
	if (!scaler)
	{
		return false;
	}

	out_image.width = frame->width;
	out_image.height = frame->height;
	out_image.metadata.width = frame->width;
	out_image.metadata.height = frame->height;
	out_image.metadata.channel_count = 4;
	out_image.pixels.resize(static_cast<size_t>(frame->width * frame->height));

	uint8_t* destination_data[4] = {reinterpret_cast<uint8_t*>(out_image.pixels.data()), nullptr, nullptr, nullptr};
	int destination_linesize[4] = {frame->width * 4, 0, 0, 0};

	sws_scale(
		scaler,
		frame->data,
		frame->linesize,
		0,
		frame->height,
		destination_data,
		destination_linesize);

	sws_freeContext(scaler);
	return true;
}

bool decode_first_frame(AVFormatContext* format_context, RgbaImage& out_image, const core::String& source_label)
{
	if (!format_context)
	{
		return false;
	}

	const int stream_index = av_find_best_stream(
		format_context,
		AVMEDIA_TYPE_VIDEO,
		-1,
		-1,
		nullptr,
		0);
	if (stream_index < 0)
	{
		return false;
	}

	AVStream* stream = format_context->streams[stream_index];
	const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
	if (!codec)
	{
		return false;
	}

	AVCodecContext* codec_context = avcodec_alloc_context3(codec);
	if (!codec_context)
	{
		return false;
	}

	if (avcodec_parameters_to_context(codec_context, stream->codecpar) < 0)
	{
		avcodec_free_context(&codec_context);
		return false;
	}

	if (avcodec_open2(codec_context, codec, nullptr) < 0)
	{
		avcodec_free_context(&codec_context);
		return false;
	}

	AVPacket* packet = av_packet_alloc();
	AVFrame* frame = av_frame_alloc();
	bool decoded = false;

	while (!decoded && av_read_frame(format_context, packet) >= 0)
	{
		if (packet->stream_index != stream_index)
		{
			av_packet_unref(packet);
			continue;
		}

		if (avcodec_send_packet(codec_context, packet) >= 0)
		{
			while (avcodec_receive_frame(codec_context, frame) >= 0)
			{
				decoded = frame_to_rgba(frame, out_image);
				break;
			}
		}

		av_packet_unref(packet);
		if (decoded)
		{
			break;
		}
	}

	out_image.metadata.source_path = source_label;
	av_frame_free(&frame);
	av_packet_free(&packet);
	avcodec_free_context(&codec_context);
	return decoded;
}

bool decode_from_format_context(
	AVFormatContext* format_context,
	RgbaImage& out_image,
	const core::String& source_label)
{
	if (!format_context)
	{
		return false;
	}

	if (avformat_find_stream_info(format_context, nullptr) < 0)
	{
		return false;
	}

	return decode_first_frame(format_context, out_image, source_label);
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
	if (!fill_metadata(path, out_image))
	{
		return false;
	}

	AVFormatContext* format_context = nullptr;
	if (avformat_open_input(&format_context, path.c_str(), nullptr, nullptr) < 0)
	{
		out_image = {};
		return false;
	}

	const bool decoded = decode_from_format_context(format_context, out_image, path);
	avformat_close_input(&format_context);
	return decoded;
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

	MemoryInput memory_input {data, size, 0};
	constexpr int kAvioBufferSize = 4096;
	uint8_t* avio_buffer = static_cast<uint8_t*>(av_malloc(kAvioBufferSize));
	if (!avio_buffer)
	{
		return false;
	}

	AVIOContext* avio_context = avio_alloc_context(
		avio_buffer,
		kAvioBufferSize,
		0,
		&memory_input,
		read_memory_packet,
		nullptr,
		seek_memory_packet);
	if (!avio_context)
	{
		av_free(avio_buffer);
		return false;
	}

	AVFormatContext* format_context = avformat_alloc_context();
	if (!format_context)
	{
		avio_context_free(&avio_context);
		return false;
	}

	format_context->pb = avio_context;
	if (avformat_open_input(&format_context, "", nullptr, nullptr) < 0)
	{
		avformat_free_context(format_context);
		return false;
	}

	const bool decoded = decode_from_format_context(format_context, out_image, source_label);
	avformat_close_input(&format_context);
	return decoded;
}

} // namespace metaagent::media
