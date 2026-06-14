#include "particle/representation_actuation.hpp"

namespace metaagent::particle {

ActuationMode RepresentationActuationPolicy::resolve_effective_mode(
	const ActuationMode mode,
	const bool hybrid_use_direct_path)
{
	switch (mode)
	{
	case ActuationMode::Direct:
		return ActuationMode::Direct;
	case ActuationMode::Parameters:
		return ActuationMode::Parameters;
	case ActuationMode::Hybrid:
		return hybrid_use_direct_path ? ActuationMode::Direct : ActuationMode::Parameters;
	default:
		return ActuationMode::Direct;
	}
}

ActuationPolicyResult RepresentationActuationPolicy::resolve(const ActuationPolicyInput& input)
{
	ActuationPolicyResult result;
	const ActuationMode effective_mode = resolve_effective_mode(
		input.preferred_mode,
		input.hybrid_use_direct_path);

	if (input.use_return_hold_blend
		&& input.blend_alpha <= input.return_release_authority_threshold)
	{
		result.force_pattern_inactive = true;
		result.override_blend_alpha = 0.0f;
		result.delivery = ActuationDelivery::ParametersWithTargets;
		// Positions are composed in C++; uploading UObject targets here has crashed Niagara
		// when the live instance is tearing down or the user params are not bound.
		result.push_target_payload = false;
		return result;
	}

	if (effective_mode == ActuationMode::Parameters)
	{
		result.delivery = ActuationDelivery::ParametersWithTargets;
		result.push_target_payload = true;
		return result;
	}

	result.delivery = input.configured_mode == ActuationMode::Hybrid
		? ActuationDelivery::HybridDirectWithScalars
		: ActuationDelivery::DirectWrite;
	return result;
}

} // namespace metaagent::particle
