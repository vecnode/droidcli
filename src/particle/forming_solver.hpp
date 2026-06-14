#pragma once

#include <memory>

#include "export.hpp"
#include "particle/forming_types.hpp"

namespace metaagent::particle {

struct FormingContext {
	int32_t global_index = -1;
	int32_t total_particle_count = 0;
	core::Vec3 baseline;
	core::Vec3 target;
	core::Vec3 pattern_center;
	float blend_alpha = 0.0f;
	float state_elapsed_seconds = 0.0f;
	float form_duration_seconds = 1.0f;
	float delta_time_seconds = 0.0f;
	const FormingSettings* settings = nullptr;
	float forming_steering_weight = 0.0f;
	core::Vec3 forming_steering_offset;
};

class IFormingSolver {
public:
	virtual ~IFormingSolver() = default;
	virtual FormingMode get_mode() const = 0;
	virtual core::Vec3 solve_position(FormingContext& context) const = 0;
};

class FormingSolverRegistry {
public:
	METAAGENT_API static void register_defaults();
	METAAGENT_API static void register_solver(std::unique_ptr<IFormingSolver> solver);
	METAAGENT_API static const IFormingSolver& get_solver(FormingMode mode);
	METAAGENT_API static core::Vec3 solve_forming_position(FormingContext& context);
};

} // namespace metaagent::particle
