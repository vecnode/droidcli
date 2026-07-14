#include "media/corpus.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace droidcli::media {
namespace {

core::String read_text_file(const core::String& path)
{
	std::ifstream stream(path, std::ios::binary);
	if (!stream)
	{
		return {};
	}

	std::ostringstream buffer;
	buffer << stream.rdbuf();
	return buffer.str();
}

core::String trim_copy(core::String value)
{
	const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
	while (!value.empty() && is_space(static_cast<unsigned char>(value.front())))
	{
		value.erase(value.begin());
	}
	while (!value.empty() && is_space(static_cast<unsigned char>(value.back())))
	{
		value.pop_back();
	}
	return value;
}

core::String to_lower_copy(core::String value)
{
	for (char& character : value)
	{
		character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
	}
	return value;
}

bool extract_json_int_field(
	const core::String& json,
	const core::String& field_name,
	int32_t& out_value,
	size_t search_from = 0)
{
	const core::String key = "\"" + field_name + "\":";
	const size_t key_index = json.find(key, search_from);
	if (key_index == core::String::npos)
	{
		return false;
	}

	size_t cursor = key_index + key.size();
	while (cursor < json.size() && std::isspace(static_cast<unsigned char>(json[cursor])) != 0)
	{
		++cursor;
	}

	const size_t end = json.find_first_of(",}\n\r", cursor);
	const core::String number_text = trim_copy(json.substr(cursor, end == core::String::npos ? core::String::npos : end - cursor));
	if (number_text.empty())
	{
		return false;
	}

	try
	{
		out_value = static_cast<int32_t>(std::stol(number_text));
		return true;
	}
	catch (...)
	{
		return false;
	}
}

bool extract_json_float_field(
	const core::String& json,
	const core::String& field_name,
	float& out_value,
	size_t search_from = 0)
{
	const core::String key = "\"" + field_name + "\":";
	const size_t key_index = json.find(key, search_from);
	if (key_index == core::String::npos)
	{
		return false;
	}

	size_t cursor = key_index + key.size();
	while (cursor < json.size() && std::isspace(static_cast<unsigned char>(json[cursor])) != 0)
	{
		++cursor;
	}

	const size_t end = json.find_first_of(",}\n\r", cursor);
	const core::String number_text = trim_copy(json.substr(cursor, end == core::String::npos ? core::String::npos : end - cursor));
	if (number_text.empty())
	{
		return false;
	}

	try
	{
		out_value = std::stof(number_text);
		return true;
	}
	catch (...)
	{
		return false;
	}
}

core::String extract_json_string_field_local(
	const core::String& json,
	const core::String& field_name,
	size_t search_from = 0)
{
	const core::String key = "\"" + field_name + "\":";
	const size_t key_index = json.find(key, search_from);
	if (key_index == core::String::npos)
	{
		return {};
	}

	size_t cursor = key_index + key.size();
	while (cursor < json.size() && std::isspace(static_cast<unsigned char>(json[cursor])) != 0)
	{
		++cursor;
	}
	if (cursor >= json.size() || json[cursor] != '"')
	{
		return {};
	}
	++cursor;

	core::String value;
	bool escaped = false;
	for (; cursor < json.size(); ++cursor)
	{
		const char character = json[cursor];
		if (escaped)
		{
			value.push_back(character);
			escaped = false;
			continue;
		}
		if (character == '\\')
		{
			escaped = true;
			continue;
		}
		if (character == '"')
		{
			break;
		}
		value.push_back(character);
	}
	return value;
}

core::String extract_section_file_key(const core::String& section)
{
	const size_t heading = section.find("## `");
	if (heading == core::String::npos)
	{
		return {};
	}

	const size_t key_start = heading + 4;
	const size_t key_end = section.find('`', key_start);
	if (key_end == core::String::npos)
	{
		return {};
	}

	const core::String raw_path = section.substr(key_start, key_end - key_start);
	return corpus_file_key_from_path(raw_path);
}

core::String extract_fenced_block(const core::String& section, const core::String& fence_language)
{
	const core::String opener = "```" + fence_language;
	const size_t block_start = section.find(opener);
	if (block_start == core::String::npos)
	{
		return {};
	}

	size_t content_start = block_start + opener.size();
	if (content_start < section.size() && section[content_start] == '\r')
	{
		++content_start;
	}
	if (content_start < section.size() && section[content_start] == '\n')
	{
		++content_start;
	}

	const size_t block_end = section.find("```", content_start);
	if (block_end == core::String::npos)
	{
		return section.substr(content_start);
	}

	return section.substr(content_start, block_end - content_start);
}

ImageCorpusEntry* find_or_create_entry(core::Array<ImageCorpusEntry>& entries, const core::String& file_key)
{
	for (ImageCorpusEntry& entry : entries)
	{
		if (to_lower_copy(entry.file_key) == to_lower_copy(file_key))
		{
			return &entry;
		}
	}

	ImageCorpusEntry created;
	created.file_key = file_key;
	entries.push_back(std::move(created));
	return &entries.back();
}

void parse_pdf_sections(const core::String& markdown, core::Array<ImageCorpusEntry>& entries)
{
	size_t cursor = 0;
	while (cursor < markdown.size())
	{
		const size_t section_start = markdown.find("## `", cursor);
		if (section_start == core::String::npos)
		{
			break;
		}

		const size_t next_section = markdown.find("\n## `", section_start + 4);
		const core::String section = markdown.substr(
			section_start,
			next_section == core::String::npos ? core::String::npos : next_section - section_start);

		const core::String file_key = extract_section_file_key(section);
		if (!file_key.empty())
		{
			ImageCorpusEntry* entry = find_or_create_entry(entries, file_key);
			entry->ocr_text = trim_copy(extract_fenced_block(section, "text"));
		}

		cursor = next_section == core::String::npos ? markdown.size() : next_section + 1;
	}
}

bool parse_bbox(const core::String& json, size_t search_from, IntRect& out_bbox)
{
	const size_t bbox_index = json.find("\"bbox\"", search_from);
	if (bbox_index == core::String::npos)
	{
		return false;
	}

	return extract_json_int_field(json, "x", out_bbox.x, bbox_index)
		&& extract_json_int_field(json, "y", out_bbox.y, bbox_index)
		&& extract_json_int_field(json, "width", out_bbox.width, bbox_index)
		&& extract_json_int_field(json, "height", out_bbox.height, bbox_index);
}

void parse_text_regions(const core::String& json, ImageCorpusEntry& entry)
{
	const size_t regions_index = json.find("\"text_regions\"");
	if (regions_index == core::String::npos)
	{
		return;
	}

	size_t cursor = regions_index;
	while (true)
	{
		const size_t object_index = json.find("\"text\"", cursor + 1);
		if (object_index == core::String::npos)
		{
			break;
		}

		const size_t region_start = json.rfind('{', object_index);
		if (region_start == core::String::npos || region_start < cursor)
		{
			break;
		}

		TextRegion region;
		extract_json_int_field(json, "id", region.id, region_start);
		region.text = extract_json_string_field_local(json, "text", region_start);
		extract_json_float_field(json, "confidence", region.confidence, region_start);
		if (!parse_bbox(json, region_start, region.bbox))
		{
			cursor = object_index + 6;
			continue;
		}

		if (!region.text.empty() && region.bbox.width > 0 && region.bbox.height > 0)
		{
			entry.text_regions.push_back(std::move(region));
		}

		cursor = object_index + 6;
	}
}

void parse_objs_sections(const core::String& markdown, core::Array<ImageCorpusEntry>& entries)
{
	size_t cursor = 0;
	while (cursor < markdown.size())
	{
		const size_t section_start = markdown.find("## `", cursor);
		if (section_start == core::String::npos)
		{
			break;
		}

		const size_t next_section = markdown.find("\n## `", section_start + 4);
		const core::String section = markdown.substr(
			section_start,
			next_section == core::String::npos ? core::String::npos : next_section - section_start);

		const core::String file_key = extract_section_file_key(section);
		if (!file_key.empty())
		{
			const core::String json_block = extract_fenced_block(section, "json");
			if (!json_block.empty())
			{
				ImageCorpusEntry* entry = find_or_create_entry(entries, file_key);
				parse_text_regions(json_block, *entry);

				const size_t image_index = json_block.find("\"image\"");
				if (image_index != core::String::npos)
				{
					extract_json_int_field(json_block, "width", entry->image_width, image_index);
					extract_json_int_field(json_block, "height", entry->image_height, image_index);
					const core::String image_path = extract_json_string_field_local(json_block, "path", image_index);
					if (!image_path.empty())
					{
						entry->source_path = image_path;
					}
				}
			}
		}

		cursor = next_section == core::String::npos ? markdown.size() : next_section + 1;
	}
}

} // namespace

core::String corpus_file_key_from_path(const core::String& path)
{
	if (path.empty())
	{
		return {};
	}

	const size_t slash = path.find_last_of("/\\");
	if (slash == core::String::npos)
	{
		return path;
	}
	return path.substr(slash + 1);
}

bool MediaCorpus::load_pair(const core::String& pdf_text_md_path, const core::String& objs_text_md_path)
{
	entries_.clear();

	const core::String pdf_markdown = read_text_file(pdf_text_md_path);
	const core::String objs_markdown = read_text_file(objs_text_md_path);
	if (pdf_markdown.empty() && objs_markdown.empty())
	{
		return false;
	}

	if (!pdf_markdown.empty())
	{
		parse_pdf_sections(pdf_markdown, entries_);
	}
	if (!objs_markdown.empty())
	{
		parse_objs_sections(objs_markdown, entries_);
	}

	std::sort(entries_.begin(), entries_.end(),
		[](const ImageCorpusEntry& a, const ImageCorpusEntry& b) {
			return to_lower_copy(a.file_key) < to_lower_copy(b.file_key);
		});

	return !entries_.empty();
}

bool MediaCorpus::load_from_directory(const core::String& data_directory)
{
	core::String root = data_directory;
	while (!root.empty() && (root.back() == '/' || root.back() == '\\'))
	{
		root.pop_back();
	}

	if (root.empty())
	{
		return false;
	}

	return load_pair(root + "/PDF_TEXT.md", root + "/OBJS_TEXT.md");
}

const ImageCorpusEntry* MediaCorpus::find_by_file_key(const core::String& file_key) const
{
	const core::String normalized = to_lower_copy(file_key);
	for (const ImageCorpusEntry& entry : entries_)
	{
		if (to_lower_copy(entry.file_key) == normalized)
		{
			return &entry;
		}
	}
	return nullptr;
}

const ImageCorpusEntry* MediaCorpus::find_by_path(const core::String& absolute_or_display_path) const
{
	return find_by_file_key(corpus_file_key_from_path(absolute_or_display_path));
}

core::String MediaCorpus::subtitle_text_for(const core::String& file_key) const
{
	const ImageCorpusEntry* entry = find_by_file_key(file_key);
	if (!entry)
	{
		return {};
	}
	return entry->ocr_text;
}

core::String MediaCorpus::subtitle_preview_for(const core::String& file_key, const std::size_t max_chars) const
{
	const core::String full_text = subtitle_text_for(file_key);
	if (full_text.empty())
	{
		return {};
	}

	core::String preview;
	preview.reserve(std::min(max_chars, full_text.size()));
	for (const char character : full_text)
	{
		if (preview.size() >= max_chars)
		{
			preview += "...";
			break;
		}
		if (character == '\r')
		{
			continue;
		}
		if (character == '\n')
		{
			if (!preview.empty())
			{
				break;
			}
			continue;
		}
		preview.push_back(character);
	}
	return preview;
}

bool MediaCorpus::focus_rect_pixels(
	const ImageCorpusEntry& entry,
	IntRect& out_rect,
	const float padding_fraction) const
{
	if (entry.text_regions.empty())
	{
		return false;
	}

	int32_t min_x = entry.text_regions.front().bbox.x;
	int32_t min_y = entry.text_regions.front().bbox.y;
	int32_t max_x = min_x + entry.text_regions.front().bbox.width;
	int32_t max_y = min_y + entry.text_regions.front().bbox.height;

	for (const TextRegion& region : entry.text_regions)
	{
		min_x = std::min(min_x, region.bbox.x);
		min_y = std::min(min_y, region.bbox.y);
		max_x = std::max(max_x, region.bbox.x + region.bbox.width);
		max_y = std::max(max_y, region.bbox.y + region.bbox.height);
	}

	const int32_t image_w = entry.image_width > 0 ? entry.image_width : max_x;
	const int32_t image_h = entry.image_height > 0 ? entry.image_height : max_y;
	const int32_t pad_x = static_cast<int32_t>((max_x - min_x) * padding_fraction);
	const int32_t pad_y = static_cast<int32_t>((max_y - min_y) * padding_fraction);

	out_rect.x = std::max(0, min_x - pad_x);
	out_rect.y = std::max(0, min_y - pad_y);
	out_rect.width = std::min(image_w - out_rect.x, (max_x - min_x) + pad_x * 2);
	out_rect.height = std::min(image_h - out_rect.y, (max_y - min_y) + pad_y * 2);

	return out_rect.width > 0 && out_rect.height > 0;
}

std::size_t MediaCorpus::pick_focus_region_index(
	const ImageCorpusEntry& entry,
	const std::size_t seed) const
{
	if (entry.text_regions.empty())
	{
		return 0;
	}

	const int64_t image_area = std::max<int64_t>(
		1,
		static_cast<int64_t>(entry.image_width) * static_cast<int64_t>(entry.image_height));
	const int64_t min_area = std::max<int64_t>(400, image_area / 500);

	struct ScoredRegion {
		std::size_t index = 0;
		double score = 0.0;
	};

	core::Array<ScoredRegion> scored;
	scored.reserve(entry.text_regions.size());

	for (std::size_t i = 0; i < entry.text_regions.size(); ++i)
	{
		const TextRegion& region = entry.text_regions[i];
		const int64_t area = static_cast<int64_t>(region.bbox.width)
			* static_cast<int64_t>(region.bbox.height);
		if (area < min_area || region.bbox.width < 8 || region.bbox.height < 8)
		{
			continue;
		}

		const double score = static_cast<double>(area)
			* static_cast<double>(std::max(0.05f, region.confidence));
		scored.push_back({i, score});
	}

	if (scored.empty())
	{
		std::size_t best = 0;
		int64_t best_area = 0;
		for (std::size_t i = 0; i < entry.text_regions.size(); ++i)
		{
			const IntRect& bbox = entry.text_regions[i].bbox;
			const int64_t area = static_cast<int64_t>(bbox.width) * static_cast<int64_t>(bbox.height);
			if (area > best_area)
			{
				best_area = area;
				best = i;
			}
		}
		return best;
	}

	std::sort(
		scored.begin(),
		scored.end(),
		[](const ScoredRegion& a, const ScoredRegion& b) { return a.score > b.score; });

	const double max_score = scored.front().score;
	core::Array<std::size_t> candidates;
	for (const ScoredRegion& region : scored)
	{
		if (region.score >= max_score * 0.45)
		{
			candidates.push_back(region.index);
		}
	}

	if (candidates.empty())
	{
		return scored.front().index;
	}

	return candidates[seed % candidates.size()];
}

bool MediaCorpus::region_focus_crop_16x9(
	const ImageCorpusEntry& entry,
	const std::size_t region_index,
	IntRect& out_rect,
	const float padding_fraction) const
{
	if (region_index >= entry.text_regions.size())
	{
		return false;
	}

	const TextRegion& region = entry.text_regions[region_index];
	const int32_t image_w = entry.image_width > 0
		? entry.image_width
		: region.bbox.x + region.bbox.width;
	const int32_t image_h = entry.image_height > 0
		? entry.image_height
		: region.bbox.y + region.bbox.height;
	if (image_w <= 0 || image_h <= 0)
	{
		return false;
	}

	int32_t x0 = region.bbox.x;
	int32_t y0 = region.bbox.y;
	int32_t x1 = region.bbox.x + region.bbox.width;
	int32_t y1 = region.bbox.y + region.bbox.height;

	const int32_t pad_x = static_cast<int32_t>((x1 - x0) * padding_fraction) + 8;
	const int32_t pad_y = static_cast<int32_t>((y1 - y0) * padding_fraction) + 8;
	x0 = std::max(0, x0 - pad_x);
	y0 = std::max(0, y0 - pad_y);
	x1 = std::min(image_w, x1 + pad_x);
	y1 = std::min(image_h, y1 + pad_y);

	const int32_t box_w = std::max(1, x1 - x0);
	const int32_t box_h = std::max(1, y1 - y0);
	constexpr float k_aspect = 16.0f / 9.0f;

	int32_t crop_w = 0;
	int32_t crop_h = 0;
	const float box_aspect = static_cast<float>(box_w) / static_cast<float>(box_h);
	if (box_aspect > k_aspect)
	{
		crop_w = box_w;
		crop_h = static_cast<int32_t>(static_cast<float>(crop_w) / k_aspect + 0.5f);
	}
	else
	{
		crop_h = box_h;
		crop_w = static_cast<int32_t>(static_cast<float>(crop_h) * k_aspect + 0.5f);
	}

	if (crop_w > image_w)
	{
		crop_w = image_w;
		crop_h = static_cast<int32_t>(static_cast<float>(crop_w) / k_aspect + 0.5f);
	}
	if (crop_h > image_h)
	{
		crop_h = image_h;
		crop_w = static_cast<int32_t>(static_cast<float>(crop_h) * k_aspect + 0.5f);
	}
	crop_w = std::min(crop_w, image_w);
	crop_h = std::min(crop_h, image_h);

	const int32_t cx = region.bbox.x + region.bbox.width / 2;
	const int32_t cy = region.bbox.y + region.bbox.height / 2;

	int32_t crop_x = cx - crop_w / 2;
	int32_t crop_y = cy - crop_h / 2;

	crop_x = std::max(0, std::min(crop_x, image_w - crop_w));
	crop_y = std::max(0, std::min(crop_y, image_h - crop_h));

	if (x0 < crop_x)
	{
		crop_x = x0;
	}
	if (y0 < crop_y)
	{
		crop_y = y0;
	}
	if (x1 > crop_x + crop_w)
	{
		crop_x = x1 - crop_w;
	}
	if (y1 > crop_y + crop_h)
	{
		crop_y = y1 - crop_h;
	}
	crop_x = std::max(0, std::min(crop_x, image_w - crop_w));
	crop_y = std::max(0, std::min(crop_y, image_h - crop_h));

	out_rect.x = crop_x;
	out_rect.y = crop_y;
	out_rect.width = crop_w;
	out_rect.height = crop_h;
	return out_rect.width > 0 && out_rect.height > 0;
}

bool MediaCorpus::build_region_mask(
	const ImageCorpusEntry& entry,
	const int32_t mask_width,
	const int32_t mask_height,
	core::Array<float>& out_weights) const
{
	if (mask_width <= 0 || mask_height <= 0 || entry.text_regions.empty())
	{
		return false;
	}

	const int32_t source_w = entry.image_width > 0 ? entry.image_width : mask_width;
	const int32_t source_h = entry.image_height > 0 ? entry.image_height : mask_height;
	out_weights.assign(static_cast<size_t>(mask_width * mask_height), 0.0f);

	for (const TextRegion& region : entry.text_regions)
	{
		const int32_t x0 = std::max(0, region.bbox.x);
		const int32_t y0 = std::max(0, region.bbox.y);
		const int32_t x1 = std::min(source_w, region.bbox.x + region.bbox.width);
		const int32_t y1 = std::min(source_h, region.bbox.y + region.bbox.height);

		const int32_t mx0 = static_cast<int32_t>(static_cast<float>(x0) / static_cast<float>(source_w) * mask_width);
		const int32_t my0 = static_cast<int32_t>(static_cast<float>(y0) / static_cast<float>(source_h) * mask_height);
		const int32_t mx1 = static_cast<int32_t>(static_cast<float>(x1) / static_cast<float>(source_w) * mask_width);
		const int32_t my1 = static_cast<int32_t>(static_cast<float>(y1) / static_cast<float>(source_h) * mask_height);

		for (int32_t y = my0; y < my1 && y < mask_height; ++y)
		{
			for (int32_t x = mx0; x < mx1 && x < mask_width; ++x)
			{
				out_weights[static_cast<size_t>(y * mask_width + x)] = 1.0f;
			}
		}
	}

	return true;
}

} // namespace droidcli::media
