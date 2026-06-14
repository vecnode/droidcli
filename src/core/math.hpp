#pragma once

#include <algorithm>
#include <cmath>

#include "core/types.hpp"

namespace metaagent::core::math {

constexpr float k_pi = 3.14159265358979323846f;
constexpr float k_two_pi = k_pi * 2.0f;
constexpr float k_epsilon = 1e-4f;

template<typename T>
constexpr T clamp(T value, T min_value, T max_value)
{
	return std::max(min_value, std::min(max_value, value));
}

constexpr float lerp(float a, float b, float alpha)
{
	return a + (b - a) * alpha;
}

METAAGENT_API float smooth_step01(float alpha);
METAAGENT_API Vec3 lerp(const Vec3& a, const Vec3& b, float alpha);
METAAGENT_API Vec3 rotate_around_axis(const Vec3& vector, const Vec3& axis, float angle_rad);
METAAGENT_API float evaluate_curve01(float normalized_time, const float* samples, int sample_count);

} // namespace metaagent::core::math
