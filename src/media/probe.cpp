#include "media/probe.hpp"

extern "C" {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244)
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
}

namespace metaagent::media {

bool probe_media_file(const core::String& path, MediaProbeInfo& out_info)
{
	out_info = MediaProbeInfo {};
	out_info.path = path;

	AVFormatContext* format_context = nullptr;
	if (avformat_open_input(&format_context, path.c_str(), nullptr, nullptr) < 0)
	{
		return false;
	}

	if (avformat_find_stream_info(format_context, nullptr) < 0)
	{
		avformat_close_input(&format_context);
		return false;
	}

	if (format_context->iformat && format_context->iformat->name)
	{
		out_info.format_name = format_context->iformat->name;
	}

	const int stream_index = av_find_best_stream(
		format_context,
		AVMEDIA_TYPE_VIDEO,
		-1,
		-1,
		nullptr,
		0);
	if (stream_index >= 0)
	{
		const AVStream* stream = format_context->streams[stream_index];
		const AVCodecParameters* parameters = stream->codecpar;
		out_info.width = parameters->width;
		out_info.height = parameters->height;
		if (parameters->codec_id != AV_CODEC_ID_NONE)
		{
			const AVCodec* codec = avcodec_find_decoder(parameters->codec_id);
			if (codec && codec->name)
			{
				out_info.codec_name = codec->name;
			}
		}

		if (stream->avg_frame_rate.den > 0)
		{
			out_info.frame_rate = av_q2d(stream->avg_frame_rate);
		}

		if (stream->duration > 0)
		{
			out_info.duration_ms = stream->duration * 1000
				* static_cast<int64_t>(stream->time_base.num)
				/ static_cast<int64_t>(stream->time_base.den);
		}
	}

	if (out_info.duration_ms <= 0 && format_context->duration > 0)
	{
		out_info.duration_ms = format_context->duration * 1000 / AV_TIME_BASE;
	}

	out_info.valid = true;
	avformat_close_input(&format_context);
	return true;
}

} // namespace metaagent::media
