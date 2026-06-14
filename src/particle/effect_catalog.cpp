#include "particle/effect_catalog.hpp"

#include "particle/state_effects.hpp"

namespace metaagent::particle {
namespace {

ParticleGuiActionSpec make_spec(
	const char* action_id,
	const char* key,
	const char* description,
	const ParticleGuiDispatchKind kind,
	const char* effect_id = "")
{
	ParticleGuiActionSpec spec;
	spec.gui_action_id = action_id;
	spec.key_label = key;
	spec.description = description;
	spec.dispatch_kind = kind;
	spec.effect_id = effect_id;
	return spec;
}

core::Array<ParticleGuiActionSpec> build_specs()
{
	core::Array<ParticleGuiActionSpec> specs;
	specs.push_back(make_spec(
		"ParticleLoadPreview",
		"F",
		"Load sdxl_latest.png preview + image shape source",
		ParticleGuiDispatchKind::LoadPreviewPng));
	specs.push_back(make_spec(
		"ParticleStepBackward",
		",",
		"Step pattern state backward",
		ParticleGuiDispatchKind::TriggerEffect,
		"PatternStepBackward"));
	specs.push_back(make_spec(
		"ParticleStepForward",
		".",
		"Step pattern state forward (Idle→Forming→Holding→Returning→Idle; mask may load silently first)",
		ParticleGuiDispatchKind::TriggerEffect,
		"PatternStepForward"));
	specs.push_back(make_spec(
		"ParticleCyclePreset",
		"B",
		"Cycle timing preset (Normal/Slow/Dramatic/Snappy/Dreamy)",
		ParticleGuiDispatchKind::TriggerEffect,
		"CyclePreset"));
	specs.push_back(make_spec(
		"ParticleCycleSampling",
		"T",
		"Cycle image sampling mode",
		ParticleGuiDispatchKind::TriggerEffect,
		"CycleSampling"));
	specs.push_back(make_spec(
		"ParticleCycleForming",
		"Y",
		"Cycle forming motion mode",
		ParticleGuiDispatchKind::TriggerEffect,
		"CycleForming"));
	specs.push_back(make_spec(
		"ParticleCycleReturning",
		"K",
		"Cycle returning motion mode",
		ParticleGuiDispatchKind::TriggerEffect,
		"CycleReturning"));
	specs.push_back(make_spec(
		"ParticleCycleOverlay",
		"Z",
		"Cycle overlay effects (Cohesion/Turbulence/Both/Off)",
		ParticleGuiDispatchKind::TriggerEffect,
		"CycleOverlay"));
	return specs;
}

} // namespace

core::Array<ParticleGuiActionSpec> particle_gui_action_specs()
{
	return build_specs();
}

const ParticleGuiActionSpec* find_particle_gui_action(const core::String& gui_action_id)
{
	for (const ParticleGuiActionSpec& spec : build_specs())
	{
		if (spec.gui_action_id == gui_action_id)
		{
			return &spec;
		}
	}
	return nullptr;
}

core::Array<app::GuiPanelRow> particle_gui_panel_rows()
{
	core::Array<app::GuiPanelRow> rows;
	for (const ParticleGuiActionSpec& spec : build_specs())
	{
		app::GuiPanelRow row;
		row.action_id = spec.gui_action_id;
		row.key_label = spec.key_label;
		row.description = spec.description;
		rows.push_back(row);
	}
	return rows;
}

} // namespace metaagent::particle
