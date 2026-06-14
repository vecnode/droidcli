#pragma once

#include "app/gui_catalog.hpp"
#include "export.hpp"

namespace metaagent::particle {

enum class ParticleGuiDispatchKind {
	LoadPreviewPng = 0,
	TriggerEffect = 1,
	ToggleStateEffect = 2,
};

struct ParticleGuiActionSpec {
	core::String gui_action_id;
	core::String key_label;
	core::String description;
	ParticleGuiDispatchKind dispatch_kind = ParticleGuiDispatchKind::TriggerEffect;
	core::String effect_id;
};

METAAGENT_API core::Array<ParticleGuiActionSpec> particle_gui_action_specs();

METAAGENT_API const ParticleGuiActionSpec* find_particle_gui_action(const core::String& gui_action_id);

METAAGENT_API core::Array<app::GuiPanelRow> particle_gui_panel_rows();

} // namespace metaagent::particle
