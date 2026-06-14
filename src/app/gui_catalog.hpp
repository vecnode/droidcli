#pragma once

#include "core/types.hpp"
#include "export.hpp"

namespace metaagent::app {

struct GuiPanelRow {
	core::String action_id;
	core::String key_label;
	core::String description;
};

struct GuiPanelSection {
	core::String section_id;
	core::String title;
	bool always_on = false;
	core::String runtime_feature;
	core::Array<GuiPanelRow> rows;
	core::Array<core::String> status_lines;
};

struct GuiPanelCatalog {
	core::Array<GuiPanelSection> sections;
};

METAAGENT_API GuiPanelCatalog build_gui_panel_catalog();

METAAGENT_API bool section_runtime_feature(
	const core::String& section_id,
	core::String& out_feature);

} // namespace metaagent::app
