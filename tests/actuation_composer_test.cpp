#include "particle/actuation_math.hpp"
#include "particle/pattern_types.hpp"
#include "particle/representation_actuation.hpp"

#include <cmath>
#include <iostream>

namespace {

bool expect_near(const float actual, const float expected, const float tolerance, const char* label)
{
	if (std::fabs(actual - expected) <= tolerance)
	{
		return true;
	}

	std::cerr << label << " expected " << expected << " got " << actual << '\n';
	return false;
}

bool expect_true(const bool value, const char* label)
{
	if (value)
	{
		return true;
	}

	std::cerr << label << " failed\n";
	return false;
}

} // namespace

int main()
{
	using namespace metaagent::particle;

	metaagent::particle::PatternConfig config;
	metaagent::core::Array<float> form_curve;
	form_curve.push_back(0.0f);
	form_curve.push_back(1.0f);

	if (!expect_near(
		ActuationMath::evaluate_phase_for_state(
			PatternState::Forming,
			0.5f,
			config,
			form_curve,
			{}),
		0.5f,
		1e-4f,
		"forming curve midpoint"))
	{
		return 1;
	}

	if (!expect_near(
		ActuationMath::evaluate_phase_for_state(
			PatternState::Returning,
			0.0f,
			config,
			{},
			{}),
		1.0f,
		1e-4f,
		"return start phase"))
	{
		return 1;
	}

	metaagent::core::Array<metaagent::core::Vec3> baseline;
	metaagent::core::Array<metaagent::core::Vec3> targets;
	baseline.push_back({0.0f, 0.0f, 0.0f});
	targets.push_back({100.0f, 0.0f, 0.0f});

	ActuationComposeInput compose_input;
	compose_input.pattern_state = PatternState::Holding;
	compose_input.blend_alpha = 0.25f;
	compose_input.baseline_world_positions = &baseline;
	compose_input.pattern_world_targets = &targets;

	const ActuationComposeResult composed =
		ActuationMath::compose_particle_world_position(compose_input, 0);
	if (!expect_true(composed.valid, "compose valid")
		|| !expect_near(composed.world_position.x, 25.0f, 1e-4f, "compose lerp x"))
	{
		return 1;
	}

	ActuationPolicyInput policy_input;
	policy_input.configured_mode = ActuationMode::Hybrid;
	policy_input.preferred_mode = ActuationMode::Hybrid;
	policy_input.hybrid_use_direct_path = true;
	const ActuationPolicyResult hybrid_policy = RepresentationActuationPolicy::resolve(policy_input);
	if (!expect_true(
		hybrid_policy.delivery == ActuationDelivery::HybridDirectWithScalars,
		"hybrid delivery"))
	{
		return 1;
	}

	policy_input.use_return_hold_blend = true;
	policy_input.blend_alpha = 0.01f;
	policy_input.return_release_authority_threshold = 0.05f;
	const ActuationPolicyResult release_policy = RepresentationActuationPolicy::resolve(policy_input);
	if (!expect_true(release_policy.force_pattern_inactive, "return release inactive")
		|| !expect_true(!release_policy.push_target_payload, "return release skips target upload"))
	{
		return 1;
	}

	return 0;
}
