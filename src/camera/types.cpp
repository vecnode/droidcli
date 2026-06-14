#include "camera/types.hpp"

#include "core/math.hpp"

#include <cmath>

namespace metaagent::camera {

core::Vec3 Bounds3::center() const
{
	return {
		(min.x + max.x) * 0.5f,
		(min.y + max.y) * 0.5f,
		(min.z + max.z) * 0.5f};
}

float Bounds3::radius_xy() const
{
	const core::Vec3 extent = max - min;
	return 0.5f * std::sqrt(extent.x * extent.x + extent.y * extent.y);
}

Bounds3 compute_bounds(const core::Array<core::Vec3>& points)
{
	Bounds3 bounds;
	if (points.empty())
	{
		return bounds;
	}

	bounds.min = points.front();
	bounds.max = points.front();
	for (const core::Vec3& point : points)
	{
		bounds.min.x = std::min(bounds.min.x, point.x);
		bounds.min.y = std::min(bounds.min.y, point.y);
		bounds.min.z = std::min(bounds.min.z, point.z);
		bounds.max.x = std::max(bounds.max.x, point.x);
		bounds.max.y = std::max(bounds.max.y, point.y);
		bounds.max.z = std::max(bounds.max.z, point.z);
	}
	return bounds;
}

CinematicStyle cycle_cinematic_style(const CinematicStyle current)
{
	switch (current)
	{
	case CinematicStyle::OscillatingHold:
		return CinematicStyle::SlowOrbit;
	case CinematicStyle::SlowOrbit:
	default:
		return CinematicStyle::OscillatingHold;
	}
}

const char* cinematic_style_label(const CinematicStyle style)
{
	switch (style)
	{
	case CinematicStyle::SlowOrbit:
		return "Slow Orbit";
	case CinematicStyle::OscillatingHold:
	default:
		return "Oscillating Hold";
	}
}

} // namespace metaagent::camera
