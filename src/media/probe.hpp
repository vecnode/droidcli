#pragma once

#include "export.hpp"
#include "core/types.hpp"

namespace droidcli::media {

struct MediaProbeInfo {
	bool valid = false;
	core::String path;
	core::String format_name;
	core::String codec_name;
	int32_t width = 0;
	int32_t height = 0;
	int64_t duration_ms = 0;
	double frame_rate = 0.0;
};

DROIDCLI_API bool probe_media_file(const core::String& path, MediaProbeInfo& out_info);

} // namespace droidcli::media
