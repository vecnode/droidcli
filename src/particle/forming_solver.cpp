#include "particle/forming_solver.hpp"

#include "core/math.hpp"

#include <cmath>
#include <memory>
#include <vector>

namespace metaagent::particle {
namespace {

using core::math::clamp;
using core::math::k_pi;
using core::math::k_two_pi;
using core::math::lerp;
using core::math::rotate_around_axis;
using core::math::smooth_step01;

core::Vec3 apply_steering_offset(const core::Vec3& position, const FormingContext& context, float blend_alpha)
{
	if (context.forming_steering_weight <= 1e-4f || context.forming_steering_offset.nearly_zero())
	{
		return position;
	}

	return position + context.forming_steering_offset * (context.forming_steering_weight * (1.0f - blend_alpha));
}

core::Vec3 solve_direct_lerp(FormingContext& context)
{
	const float alpha = smooth_step01(context.blend_alpha);
	core::Vec3 position = lerp(context.baseline, context.target, alpha);
	return apply_steering_offset(position, context, alpha);
}

class DirectLerpFormingSolver final : public IFormingSolver {
public:
	FormingMode get_mode() const override { return FormingMode::DirectLerp; }
	core::Vec3 solve_position(FormingContext& context) const override { return solve_direct_lerp(context); }
};

class ArcLiftFormingSolver final : public IFormingSolver {
public:
	FormingMode get_mode() const override { return FormingMode::ArcLift; }
	core::Vec3 solve_position(FormingContext& context) const override
	{
		const float alpha = smooth_step01(context.blend_alpha);
		core::Vec3 position = lerp(context.baseline, context.target, alpha);
		const float lift_height = context.settings ? context.settings->arc_lift_height_cm : 80.0f;
		position.z += std::sin(alpha * k_pi) * lift_height;
		return apply_steering_offset(position, context, alpha);
	}
};

class SpiralInFormingSolver final : public IFormingSolver {
public:
	FormingMode get_mode() const override { return FormingMode::SpiralIn; }
	core::Vec3 solve_position(FormingContext& context) const override
	{
		const float alpha = smooth_step01(context.blend_alpha);
		const float turns = context.settings ? context.settings->spiral_turns : 1.5f;
		const core::Vec3 radial_start = context.baseline - context.pattern_center;
		const core::Vec3 radial_end = context.target - context.pattern_center;
		const float spin_radians = turns * k_two_pi * alpha;
		const core::Vec3 up {0.0f, 0.0f, 1.0f};
		const core::Vec3 spun_start = rotate_around_axis(radial_start, up, spin_radians);
		const core::Vec3 spiraled = context.pattern_center + lerp(spun_start, radial_end, alpha);
		core::Vec3 position = lerp(lerp(context.baseline, context.target, alpha), spiraled, 0.65f);
		return apply_steering_offset(position, context, alpha);
	}
};

float resolve_staggered_particle_alpha(const FormingContext& context)
{
	const float spread = context.settings ? clamp(context.settings->wave_spread, 0.0f, 0.9f) : 0.45f;
	const int32_t total_count = std::max(1, context.total_particle_count);
	const float normalized_index = context.global_index >= 0 && total_count > 1
		? static_cast<float>(context.global_index) / static_cast<float>(total_count - 1)
		: 0.0f;
	const float window = std::max(1e-4f, 1.0f - spread);
	return clamp((context.blend_alpha - normalized_index * spread) / window, 0.0f, 1.0f);
}

float resolve_spring_chase_alpha(const FormingContext& context)
{
	const float alpha = smooth_step01(context.blend_alpha);
	const float overshoot = context.settings ? clamp(context.settings->spring_overshoot, 0.0f, 0.5f) : 0.12f;
	const float back_ease = alpha + overshoot * std::sin(alpha * k_pi) * (1.0f - alpha);
	return clamp(back_ease, 0.0f, 1.0f + overshoot);
}

class StaggeredWaveFormingSolver final : public IFormingSolver {
public:
	FormingMode get_mode() const override { return FormingMode::StaggeredWave; }
	core::Vec3 solve_position(FormingContext& context) const override
	{
		const float alpha = smooth_step01(resolve_staggered_particle_alpha(context));
		core::Vec3 position = lerp(context.baseline, context.target, alpha);
		return apply_steering_offset(position, context, alpha);
	}
};

class SpringChaseFormingSolver final : public IFormingSolver {
public:
	FormingMode get_mode() const override { return FormingMode::SpringChase; }
	core::Vec3 solve_position(FormingContext& context) const override
	{
		const float alpha = resolve_spring_chase_alpha(context);
		core::Vec3 position = lerp(context.baseline, context.target, alpha);
		return apply_steering_offset(position, context, clamp(alpha, 0.0f, 1.0f));
	}
};

std::vector<std::unique_ptr<IFormingSolver>>& solvers()
{
	static std::vector<std::unique_ptr<IFormingSolver>> instance;
	return instance;
}

} // namespace

void FormingSolverRegistry::register_defaults()
{
	if (!solvers().empty())
	{
		return;
	}

	solvers().push_back(std::make_unique<DirectLerpFormingSolver>());
	solvers().push_back(std::make_unique<ArcLiftFormingSolver>());
	solvers().push_back(std::make_unique<SpiralInFormingSolver>());
	solvers().push_back(std::make_unique<StaggeredWaveFormingSolver>());
	solvers().push_back(std::make_unique<SpringChaseFormingSolver>());
}

void FormingSolverRegistry::register_solver(std::unique_ptr<IFormingSolver> solver)
{
	register_defaults();
	if (solver)
	{
		solvers().push_back(std::move(solver));
	}
}

const IFormingSolver& FormingSolverRegistry::get_solver(const FormingMode mode)
{
	register_defaults();
	const FormingMode sanitized = FormingSettings::sanitize_mode(mode);
	for (const auto& solver : solvers())
	{
		if (solver && solver->get_mode() == sanitized)
		{
			return *solver;
		}
	}
	return *solvers().front();
}

core::Vec3 FormingSolverRegistry::solve_forming_position(FormingContext& context)
{
	const FormingMode mode = context.settings
		? FormingSettings::sanitize_mode(context.settings->mode)
		: FormingMode::DirectLerp;
	return get_solver(mode).solve_position(context);
}

} // namespace metaagent::particle
