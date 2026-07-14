#include "core/math.hpp"

#include <cmath>

namespace droidcli::core::math {

float smooth_step01(float alpha)
{
	const float clamped = clamp(alpha, 0.0f, 1.0f);
	return clamped * clamped * (3.0f - 2.0f * clamped);
}

Vec3 lerp(const Vec3& a, const Vec3& b, float alpha)
{
	return {
		lerp(a.x, b.x, alpha),
		lerp(a.y, b.y, alpha),
		lerp(a.z, b.z, alpha)};
}

Vec3 rotate_around_axis(const Vec3& vector, const Vec3& axis, float angle_rad)
{
	const Vec3 normalized_axis = axis.normalized();
	const float cos_angle = std::cos(angle_rad);
	const float sin_angle = std::sin(angle_rad);
	const float one_minus_cos = 1.0f - cos_angle;

	const float x = normalized_axis.x;
	const float y = normalized_axis.y;
	const float z = normalized_axis.z;

	const Vec3 rotated = {
		(cos_angle + x * x * one_minus_cos) * vector.x
			+ (x * y * one_minus_cos - z * sin_angle) * vector.y
			+ (x * z * one_minus_cos + y * sin_angle) * vector.z,
		(y * x * one_minus_cos + z * sin_angle) * vector.x
			+ (cos_angle + y * y * one_minus_cos) * vector.y
			+ (y * z * one_minus_cos - x * sin_angle) * vector.z,
		(z * x * one_minus_cos - y * sin_angle) * vector.x
			+ (z * y * one_minus_cos + x * sin_angle) * vector.y
			+ (cos_angle + z * z * one_minus_cos) * vector.z};

	return rotated;
}

float evaluate_curve01(float normalized_time, const float* samples, int sample_count)
{
	if (!samples || sample_count <= 0)
	{
		return clamp(normalized_time, 0.0f, 1.0f);
	}

	if (sample_count == 1)
	{
		return clamp(samples[0], 0.0f, 1.0f);
	}

	const float clamped_time = clamp(normalized_time, 0.0f, 1.0f);
	const float scaled = clamped_time * static_cast<float>(sample_count - 1);
	const int lower_index = static_cast<int>(std::floor(scaled));
	const int upper_index = std::min(lower_index + 1, sample_count - 1);
	const float fraction = scaled - static_cast<float>(lower_index);
	return clamp(lerp(samples[lower_index], samples[upper_index], fraction), 0.0f, 1.0f);
}

} // namespace droidcli::core::math
