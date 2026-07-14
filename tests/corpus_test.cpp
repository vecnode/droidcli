#include "media/corpus.hpp"

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>

namespace {

void write_fixture(const char* path, const char* contents)
{
	std::ofstream out(path, std::ios::binary);
	out << contents;
}

} // namespace

int main()
{
	write_fixture("corpus_pdf_fixture.md", R"(## `Release_1_PNG\059UAP00011-1.png`

```text
CONFIDENTIAL
Sample OCR body
```
)");

	write_fixture("corpus_objs_fixture.md", R"(## `Release_1_PNG\059UAP00011-1.png`

```json
{
  "image": { "width": 1275, "height": 1650 },
  "text_regions": [
    {
      "id": 1,
      "text": "CONFIDENTIAL",
      "confidence": 0.9,
      "bbox": { "x": 38, "y": 22, "width": 176, "height": 30 }
    }
  ]
}
```
)");

	droidcli::media::MediaCorpus corpus;
	assert(corpus.load_pair("corpus_pdf_fixture.md", "corpus_objs_fixture.md"));

	const droidcli::media::ImageCorpusEntry* entry = corpus.find_by_file_key("059UAP00011-1.png");
	assert(entry != nullptr);
	assert(entry->ocr_text.find("CONFIDENTIAL") != droidcli::core::String::npos);
	assert(entry->text_regions.size() == 1);
	assert(entry->image_width == 1275);

	droidcli::media::IntRect focus {};
	assert(corpus.focus_rect_pixels(*entry, focus));

	const std::size_t region_index = corpus.pick_focus_region_index(*entry, 0);
	assert(region_index < entry->text_regions.size());

	droidcli::media::IntRect region_crop {};
	assert(corpus.region_focus_crop_16x9(*entry, region_index, region_crop));
	assert(region_crop.width > 0);
	assert(region_crop.height > 0);

	if (const char* integration_dir = std::getenv("DROIDCLI_CORPUS_TEST_DATA"))
	{
		droidcli::media::MediaCorpus integration;
		if (integration.load_from_directory(integration_dir))
		{
			assert(integration.size() > 1);
		}
	}

	std::cout << "corpus_test ok\n";
	return 0;
}
