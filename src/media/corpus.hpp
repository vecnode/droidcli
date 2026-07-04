#pragma once

#include "core/types.hpp"
#include "export.hpp"

#include <cstdint>

namespace metaagent::media {

struct IntRect {
	int32_t x = 0;
	int32_t y = 0;
	int32_t width = 0;
	int32_t height = 0;
};

struct TextRegion {
	int32_t id = 0;
	core::String text;
	float confidence = 0.0f;
	IntRect bbox;
};

/// OCR + detection metadata for one source image, keyed by filename.
struct ImageCorpusEntry {
	core::String file_key;
	core::String source_path;
	int32_t image_width = 0;
	int32_t image_height = 0;
	core::String ocr_text;
	core::Array<TextRegion> text_regions;
};

/// Loads companion markdown corpora (PDF_TEXT.md + OBJS_TEXT.md) for media clips.
class MediaCorpus {
public:
	METAAGENT_API bool load_pair(
		const core::String& pdf_text_md_path,
		const core::String& objs_text_md_path);

	METAAGENT_API bool load_from_directory(const core::String& data_directory);

	METAAGENT_API bool empty() const { return entries_.empty(); }
	METAAGENT_API std::size_t size() const { return entries_.size(); }

	METAAGENT_API const ImageCorpusEntry* find_by_file_key(const core::String& file_key) const;
	METAAGENT_API const ImageCorpusEntry* find_by_path(const core::String& absolute_or_display_path) const;

	/// Full OCR text for subtitles / downstream LLM streaming.
	METAAGENT_API core::String subtitle_text_for(const core::String& file_key) const;

	/// Short line for on-screen overlay.
	METAAGENT_API core::String subtitle_preview_for(
		const core::String& file_key,
		std::size_t max_chars = 160) const;

	/// Union of text-region boxes (pixel coords) with padding for image centering/crop.
	METAAGENT_API bool focus_rect_pixels(
		const ImageCorpusEntry& entry,
		IntRect& out_rect,
		float padding_fraction = 0.05f) const;

	/// Picks one text region to frame (seed is typically the clip index).
	METAAGENT_API std::size_t pick_focus_region_index(
		const ImageCorpusEntry& entry,
		std::size_t seed = 0) const;

	/// 16:9 crop centered on a single text region (pixel coords).
	METAAGENT_API bool region_focus_crop_16x9(
		const ImageCorpusEntry& entry,
		std::size_t region_index,
		IntRect& out_rect,
		float padding_fraction = 0.12f) const;

	/// Mask weights (1 inside text bboxes, 0 elsewhere) for highlight use.
	METAAGENT_API bool build_region_mask(
		const ImageCorpusEntry& entry,
		int32_t mask_width,
		int32_t mask_height,
		core::Array<float>& out_weights) const;

private:
	core::Array<ImageCorpusEntry> entries_;
};

METAAGENT_API core::String corpus_file_key_from_path(const core::String& path);

} // namespace metaagent::media
