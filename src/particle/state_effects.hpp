#pragma once

#include "export.hpp"
#include "particle/pattern_types.hpp"

namespace metaagent::particle {

namespace state_effect_ids {
constexpr const char* Cohesion = "StateEffectCohesion";
constexpr const char* Turbulence = "StateEffectTurbulence";
} // namespace state_effect_ids

enum class StateEffectKind : uint8_t {
	RadialCohesion = 0,
	TurbulentWake = 1,
};

struct StateEffectDefinition {
	core::String effect_id;
	StateEffectKind kind = StateEffectKind::RadialCohesion;
	uint32_t allowed_states_mask = 0;
	float strength = 1.0f;
	float frequency_hz = 0.8f;
	float damping = 0.85f;
	float max_offset_cm = 8.0f;
	float attack_seconds = 0.3f;
};

struct StateEffectTickContext {
	PatternState pattern_state = PatternState::Idle;
	core::Vec3 pattern_center;
	float state_elapsed_seconds = 0.0f;
	float delta_time_seconds = 0.0f;
	const core::Array<core::Vec3>* composed_positions = nullptr;
};

struct StateEffectTriggerResult {
	bool success = false;
	bool now_active = false;
	core::String user_message;
};

class StateEffectStack {
public:
	METAAGENT_API void reset();

	METAAGENT_API StateEffectTriggerResult toggle(
		const core::String& effect_id,
		PatternState current_state);

	METAAGENT_API void tick(const StateEffectTickContext& context);

	METAAGENT_API void apply_to_positions(
		core::Array<core::Vec3>& positions,
		const core::Array<core::Vec3>& composed_positions) const;

	METAAGENT_API float emphasis_multiplier() const;

	METAAGENT_API bool is_active(const core::String& effect_id) const;

	METAAGENT_API const core::Array<core::Vec3>& offsets() const { return offsets_; }

private:
	struct ActiveEffect {
		StateEffectDefinition definition;
		float elapsed_seconds = 0.0f;
	};

	void ensure_buffers(const int32_t particle_count);
	void integrate_ambient_breathing(const StateEffectTickContext& context, const float delta_time);
	void integrate_cohesion(const ActiveEffect& effect, const StateEffectTickContext& context);
	void integrate_turbulence(const ActiveEffect& effect, const StateEffectTickContext& context);
	float envelope_for(const ActiveEffect& effect) const;

	core::Array<ActiveEffect> active_effects_;
	core::Array<core::Vec3> offsets_;
	float ambient_breathing_elapsed_seconds_ = 0.0f;
};

METAAGENT_API const StateEffectDefinition* find_state_effect_definition(const core::String& effect_id);

METAAGENT_API uint32_t state_effect_mask_for(PatternState state);

METAAGENT_API bool state_effect_allows_state(uint32_t allowed_mask, PatternState state);

} // namespace metaagent::particle
