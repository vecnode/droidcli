#include "media/mask_cache.hpp"

#include "media/decode.hpp"
#include "media/pipeline.hpp"

#include <cmath>
#include <functional>

namespace metaagent::media {
namespace {

bool nearly_equal(const float a, const float b)
{
	return std::fabs(a - b) <= 1e-4f;
}

bool params_equal(const particle::ImageMaskBuildParams& a, const particle::ImageMaskBuildParams& b)
{
	return a.source_image_path == b.source_image_path
		&& a.source_file_timestamp == b.source_file_timestamp
		&& a.source_file_size == b.source_file_size
		&& a.image_sampling_mode == b.image_sampling_mode
		&& nearly_equal(a.alpha_threshold, b.alpha_threshold)
		&& nearly_equal(a.edge_threshold, b.edge_threshold)
		&& a.use_luminance == b.use_luminance
		&& a.sample_resolution == b.sample_resolution
		&& nearly_equal(a.grayscale_gamma, b.grayscale_gamma)
		&& nearly_equal(a.density_grid_scale, b.density_grid_scale)
		&& nearly_equal(a.target_jitter_normalized, b.target_jitter_normalized)
		&& a.desired_point_count == b.desired_point_count;
}

} // namespace

bool MaskBuildCache::CacheKey::operator==(const CacheKey& other) const
{
	return source_path == other.source_path
		&& file_size == other.file_size
		&& file_modified_unix == other.file_modified_unix
		&& params_equal(params, other.params);
}

uint64_t MaskBuildCache::hash_key(const CacheKey& key)
{
	std::hash<core::String> string_hash;
	uint64_t hash = string_hash(key.source_path);
	hash = hash * 1315423911u + static_cast<uint64_t>(key.file_size);
	hash = hash * 1315423911u + static_cast<uint64_t>(key.file_modified_unix);
	hash = hash * 1315423911u + static_cast<uint64_t>(key.params.desired_point_count);
	hash = hash * 1315423911u + static_cast<uint64_t>(key.params.sample_resolution);
	return hash;
}

MaskBuildCache& MaskBuildCache::instance()
{
	static MaskBuildCache cache;
	return cache;
}

void MaskBuildCache::invalidate_all()
{
	keys_.clear();
	entries_.clear();
}

void MaskBuildCache::invalidate_path(const core::String& source_path)
{
	for (size_t index = 0; index < keys_.size();)
	{
		if (keys_[index].source_path == source_path)
		{
			keys_.erase(keys_.begin() + static_cast<std::ptrdiff_t>(index));
			entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));
		}
		else
		{
			++index;
		}
	}
}

MaskBuildCache::CacheKey MaskBuildCache::make_key(
	const core::String& source_path,
	const particle::ImageMaskBuildParams& params)
{
	CacheKey key;
	key.source_path = source_path;
	key.params = params;
	get_file_identity(source_path, key.file_size, key.file_modified_unix);
	key.params.source_file_size = key.file_size;
	key.params.source_file_timestamp = key.file_modified_unix;
	return key;
}

MaskBuildStatus MaskBuildCache::request_build(
	const core::String& source_path,
	const particle::ImageMaskBuildParams& params,
	const bool force_reload_image)
{
	if (source_path.empty() || params.desired_point_count <= 0)
	{
		return MaskBuildStatus::Unavailable;
	}

	const CacheKey key = make_key(source_path, params);
	for (size_t index = 0; index < keys_.size(); ++index)
	{
		if (keys_[index] == key)
		{
			return entries_[index].status;
		}
	}

	MaskBuildResult built;
	const bool success = build_mask_from_file(source_path, key.params, built, force_reload_image);

	CacheEntry entry;
	entry.status = success ? MaskBuildStatus::Ready : MaskBuildStatus::Failed;
	entry.result = built;

	keys_.push_back(key);
	entries_.push_back(entry);
	return entry.status;
}

MaskCacheLookup MaskBuildCache::resolve(
	const core::String& source_path,
	const particle::ImageMaskBuildParams& params) const
{
	MaskCacheLookup lookup;
	if (source_path.empty() || params.desired_point_count <= 0)
	{
		lookup.status = MaskBuildStatus::Unavailable;
		return lookup;
	}

	const CacheKey key = make_key(source_path, params);

	for (size_t index = 0; index < keys_.size(); ++index)
	{
		if (keys_[index] == key)
		{
			lookup.status = entries_[index].status;
			lookup.result = entries_[index].result;
			return lookup;
		}
	}

	lookup.status = MaskBuildStatus::Unavailable;
	return lookup;
}

bool MaskBuildCache::is_ready(
	const core::String& source_path,
	const particle::ImageMaskBuildParams& params) const
{
	const MaskCacheLookup lookup = resolve(source_path, params);
	return lookup.status == MaskBuildStatus::Ready && lookup.result.success;
}

} // namespace metaagent::media
