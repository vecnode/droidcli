#include "particle/pattern_types.hpp"

namespace metaagent::particle {

PatternConfig PatternConfig::make_from_preset(const PatternPreset preset)
{
	PatternConfig config;
	config.apply_preset(preset);
	return config;
}

void PatternConfig::apply_preset(const PatternPreset preset)
{
	active_preset = preset;

	switch (preset)
	{
	case PatternPreset::Slow:
		form_duration_seconds = 3.0f;
		hold_duration_seconds = 1.5f;
		return_duration_seconds = 3.0f;
		hold_pulse_amplitude = 0.05f;
		hold_pulse_frequency_hz = 0.6f;
		break;
	case PatternPreset::Dramatic:
		form_duration_seconds = 4.0f;
		hold_duration_seconds = 2.0f;
		return_duration_seconds = 5.0f;
		hold_pulse_amplitude = 0.12f;
		hold_pulse_frequency_hz = 0.9f;
		break;
	case PatternPreset::Snappy:
		form_duration_seconds = 0.7f;
		hold_duration_seconds = 0.35f;
		return_duration_seconds = 0.9f;
		hold_pulse_amplitude = 0.035f;
		hold_pulse_frequency_hz = 1.1f;
		forming.mode = FormingMode::StaggeredWave;
		forming.wave_spread = 0.35f;
		break;
	case PatternPreset::Dreamy:
		form_duration_seconds = 5.0f;
		hold_duration_seconds = 2.5f;
		return_duration_seconds = 4.0f;
		hold_pulse_amplitude = 0.06f;
		hold_pulse_frequency_hz = 0.45f;
		forming.mode = FormingMode::SpringChase;
		forming.spring_overshoot = 0.14f;
		break;
	case PatternPreset::Normal:
		form_duration_seconds = 1.5f;
		hold_duration_seconds = 0.5f;
		return_duration_seconds = 1.5f;
		hold_pulse_amplitude = 0.04f;
		hold_pulse_frequency_hz = 0.75f;
		break;
	case PatternPreset::Custom:
	default:
		break;
	}
}

void PatternConfig::cycle_preset()
{
	switch (active_preset)
	{
	case PatternPreset::Normal:
		apply_preset(PatternPreset::Slow);
		break;
	case PatternPreset::Slow:
		apply_preset(PatternPreset::Dramatic);
		break;
	case PatternPreset::Dramatic:
		apply_preset(PatternPreset::Snappy);
		break;
	case PatternPreset::Snappy:
		apply_preset(PatternPreset::Dreamy);
		break;
	case PatternPreset::Dreamy:
	case PatternPreset::Custom:
	default:
		apply_preset(PatternPreset::Normal);
		break;
	}
}

core::String PatternConfig::get_preset_display_name() const
{
	switch (active_preset)
	{
	case PatternPreset::Slow: return "Slow";
	case PatternPreset::Dramatic: return "Dramatic";
	case PatternPreset::Snappy: return "Snappy";
	case PatternPreset::Dreamy: return "Dreamy";
	case PatternPreset::Custom: return "Custom";
	case PatternPreset::Normal:
	default: return "Normal";
	}
}

} // namespace metaagent::particle
