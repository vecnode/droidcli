#pragma once

#include <cstdint>

#include "export.hpp"
#include "media/pipeline.hpp"

namespace metaagent::media {

enum class MaskBuildStatus : uint8_t {
	Ready,
	Failed,
	Unavailable
};

struct MaskCacheLookup {
	MaskBuildStatus status = MaskBuildStatus::Unavailable;
	MaskBuildResult result;
};

class MaskBuildCache {
public:
	METAAGENT_API static MaskBuildCache& instance();

	METAAGENT_API void invalidate_all();

	METAAGENT_API void invalidate_path(const core::String& source_path);

	METAAGENT_API MaskBuildStatus request_build(
		const core::String& source_path,
		const particle::ImageMaskBuildParams& params,
		bool force_reload_image = false);

	METAAGENT_API MaskCacheLookup resolve(
		const core::String& source_path,
		const particle::ImageMaskBuildParams& params) const;

	METAAGENT_API bool is_ready(
		const core::String& source_path,
		const particle::ImageMaskBuildParams& params) const;

private:
	struct CacheKey {
		core::String source_path;
		int64_t file_size = 0;
		int64_t file_modified_unix = 0;
		particle::ImageMaskBuildParams params;

		bool operator==(const CacheKey& other) const;
	};

	struct CacheEntry {
		MaskBuildStatus status = MaskBuildStatus::Unavailable;
		MaskBuildResult result;
	};

	static uint64_t hash_key(const CacheKey& key);

	static CacheKey make_key(
		const core::String& source_path,
		const particle::ImageMaskBuildParams& params);

	core::Array<CacheKey> keys_;
	core::Array<CacheEntry> entries_;
};

} // namespace metaagent::media
