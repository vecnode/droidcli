#include "particle/shape_builder.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {

metaagent::particle::PatternConfig make_default_config()
{
	metaagent::particle::PatternConfig config;
	config.shape.shape_width_cm = 100.0f;
	config.shape.procedural_sample_count = 16;
	return config;
}

metaagent::particle::ShapeContext make_baseline_context()
{
	metaagent::particle::ShapeContext context;
	context.baseline_world_positions = {
		{0.0f, 0.0f, 0.0f},
		{100.0f, 0.0f, 0.0f},
		{0.0f, 100.0f, 0.0f},
		{100.0f, 100.0f, 0.0f},
	};
	return context;
}

} // namespace

int main()
{
	using metaagent::core::Vec3;
	using metaagent::particle::ShapeBuilder;
	using metaagent::particle::ShapeBuildResult;
	using metaagent::particle::ShapeType;

	{
		const metaagent::particle::PatternConfig config = make_default_config();
		const metaagent::particle::ShapeContext context = make_baseline_context();
		const metaagent::core::Array<Vec3> polyline = {
			{50.0f, 50.0f, 0.0f},
			{150.0f, 50.0f, 0.0f},
			{150.0f, 150.0f, 0.0f},
		};

		ShapeBuildResult result;
		assert(ShapeBuilder::build_polyline_path_targets(config, context, polyline, result));
		assert(result.success);
		assert(result.resolved_shape == ShapeType::SplinePath);
		assert(result.shape_point_count == 3);
		assert(result.pattern_world_targets.size() == context.baseline_world_positions.size());
	}

	{
		const metaagent::particle::PatternConfig config = make_default_config();
		const metaagent::particle::ShapeContext context = make_baseline_context();
		ShapeBuildResult result;
		assert(ShapeBuilder::build_bounds_grid_targets(
			config,
			context,
			{200.0f, 200.0f, 0.0f},
			400.0f,
			200.0f,
			result));
		assert(result.success);
		assert(result.resolved_shape == ShapeType::MeshSilhouette);
		assert(result.shape_point_count == 16);
		assert(std::fabs(result.shape_frame.origin.x - 200.0f) < 0.01f);
		assert(std::fabs(result.shape_frame.extents_cm.x - 400.0f) < 0.01f);
		assert(std::fabs(result.shape_frame.extents_cm.y - 200.0f) < 0.01f);
	}

	{
		const metaagent::particle::PatternConfig config = make_default_config();
		const metaagent::particle::ShapeContext context = make_baseline_context();
		const metaagent::core::Array<Vec3> local_points = {
			{-0.25f, 0.25f, 0.0f},
			{0.25f, 0.25f, 0.0f},
			{0.0f, -0.25f, 0.0f},
		};

		ShapeBuildResult result;
		assert(ShapeBuilder::build_silhouette_from_local_points(
			config,
			context,
			local_points,
			512,
			512,
			result,
			"test extraction"));
		assert(result.success);
		assert(result.resolved_shape == ShapeType::ImageSilhouette);
		assert(result.pattern_world_targets.size() == context.baseline_world_positions.size());
	}

	std::cout << "shape_builder_polyline_test passed\n";
	return 0;
}
