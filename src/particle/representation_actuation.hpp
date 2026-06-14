#pragma once

#include "export.hpp"
#include "particle/pattern_types.hpp"
#include "particle/representation_types.hpp"

namespace metaagent::particle {

enum class ActuationDelivery : uint8_t {
	ParametersWithTargets = 0,
	ParametersOnly,
	DirectWrite,
	HybridDirectWithScalars
};

struct ActuationPolicyInput {
	ActuationMode configured_mode = ActuationMode::Hybrid;
	ActuationMode preferred_mode = ActuationMode::Hybrid;
	bool hybrid_use_direct_path = true;
	bool use_return_hold_blend = false;
	float blend_alpha = 0.0f;
	float return_release_authority_threshold = 0.05f;
};

struct ActuationPolicyResult {
	ActuationDelivery delivery = ActuationDelivery::DirectWrite;
	bool force_pattern_inactive = false;
	float override_blend_alpha = 0.0f;
	bool push_target_payload = false;
};

class RepresentationActuationPolicy {
public:
	METAAGENT_API static ActuationMode resolve_effective_mode(
		ActuationMode mode,
		bool hybrid_use_direct_path = true);
	METAAGENT_API static ActuationPolicyResult resolve(const ActuationPolicyInput& input);
};

} // namespace metaagent::particle
