#include "app/gui_catalog.hpp"

#include "particle/effect_catalog.hpp"

namespace metaagent::app {
namespace {

GuiPanelRow make_row(const char* action_id, const char* key, const char* description)
{
	GuiPanelRow row;
	row.action_id = action_id;
	row.key_label = key;
	row.description = description;
	return row;
}

GuiPanelSection make_section(
	const char* section_id,
	const char* title,
	const bool always_on,
	const char* runtime_feature,
	const core::Array<GuiPanelRow>& rows,
	const core::Array<core::String>& status_lines = {})
{
	GuiPanelSection section;
	section.section_id = section_id;
	section.title = title;
	section.always_on = always_on;
	section.runtime_feature = runtime_feature;
	section.rows = rows;
	section.status_lines = status_lines;
	return section;
}

} // namespace

GuiPanelCatalog build_gui_panel_catalog()
{
	GuiPanelCatalog catalog;

	{
		core::Array<GuiPanelRow> rows;
		rows.push_back(make_row("ToggleHelpPanel", "Q", "Toggle controls panel"));
		rows.push_back(make_row("QuitApplication", "Esc", "Quit application"));
		catalog.sections.push_back(make_section("GUI", "GUI Runtime", true, "ui", rows));
	}

	{
		core::Array<GuiPanelRow> rows;
		rows.push_back(make_row("ToggleCinematicCamera", "O", "Toggle cinematic camera"));
		rows.push_back(make_row("FocusParticleCamera", "P", "Focus cinematic on particles"));
		rows.push_back(make_row("CycleCinematicStyle", "V", "Cycle cinematic style (Hold / Slow orbit)"));

		core::Array<core::String> status;
		status.push_back("Wheel: zoom camera distance when panel is hidden");
		catalog.sections.push_back(make_section("Camera", "Camera Runtime", false, "camera", rows, status));
	}

	{
		core::Array<GuiPanelRow> rows;
		rows.push_back(make_row("StartAudio", "H", "Send HTTP 'start audio' to platform"));
		rows.push_back(make_row("StartImage", "G", "Send HTTP 'start image' to platform"));

		core::Array<core::String> status;
		status.push_back("Requires Networking runtime START and platform forwarding enabled");
		catalog.sections.push_back(make_section("Networking", "Networking Runtime", false, "networking", rows, status));
	}

	{
		core::Array<GuiPanelRow> rows = particle::particle_gui_panel_rows();
		catalog.sections.push_back(make_section("Particle", "Particle Runtime", false, "particle", rows));
	}

	{
		core::Array<GuiPanelRow> rows;
		rows.push_back(make_row("ToggleAutopilot", "I", "Toggle AI autopilot for current pawn"));
		catalog.sections.push_back(make_section("AI", "AI Runtime", false, "ai", rows));
	}

	{
		core::Array<GuiPanelRow> rows;
		rows.push_back(make_row("ToggleRecording", "J", "Start/stop video capture"));
		rows.push_back(make_row("ReportRecording", "U", "Print recording status to log"));

		core::Array<core::String> status;
		status.push_back("MovieSceneCapture AVI output under Saved/Renders");
		catalog.sections.push_back(make_section("Recording", "Recording Runtime", false, "recording", rows, status));
	}

	return catalog;
}

bool section_runtime_feature(const core::String& section_id, core::String& out_feature)
{
	if (section_id == "GUI")
	{
		out_feature = "ui";
		return true;
	}
	if (section_id == "Camera")
	{
		out_feature = "camera";
		return true;
	}
	if (section_id == "Networking")
	{
		out_feature = "networking";
		return true;
	}
	if (section_id == "Particle")
	{
		out_feature = "particle";
		return true;
	}
	if (section_id == "AI")
	{
		out_feature = "ai";
		return true;
	}
	if (section_id == "Recording")
	{
		out_feature = "recording";
		return true;
	}
	return false;
}

} // namespace metaagent::app
