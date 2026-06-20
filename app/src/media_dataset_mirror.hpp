#pragma once

#include "metaagent.h"

namespace metaagent::app_host {

class MediaDatasetMirror {
public:
	void set_preferred_data_directory(const core::String& data_directory);
	void remember_remote_clips_json(const core::String& json);
	core::String build_clips_json_fallback();

private:
	void ensure_local_index();

	core::String preferred_data_directory_;
	core::String cached_remote_clips_json_;
	core::String resolved_data_directory_;
	core::Array<core::String> local_clip_paths_;
	bool local_index_attempted_ = false;
};

} // namespace metaagent::app_host
