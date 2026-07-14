#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "export.hpp"

namespace droidcli::core {

struct Vec2 {
	float x = 0.0f;
	float y = 0.0f;

	constexpr Vec2() = default;
	constexpr Vec2(float in_x, float in_y) : x(in_x), y(in_y) {}
};

struct Vec3 {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;

	constexpr Vec3() = default;
	constexpr Vec3(float in_x, float in_y, float in_z) : x(in_x), y(in_y), z(in_z) {}

	Vec3 operator+(const Vec3& other) const { return {x + other.x, y + other.y, z + other.z}; }
	Vec3 operator-(const Vec3& other) const { return {x - other.x, y - other.y, z - other.z}; }
	Vec3 operator*(float scale) const { return {x * scale, y * scale, z * scale}; }
	Vec3& operator+=(const Vec3& other);
	Vec3& operator-=(const Vec3& other);

	float dot(const Vec3& other) const;
	float length_squared() const;
	float length() const;
	Vec3 normalized(float fallback_x = 0.0f, float fallback_y = 0.0f, float fallback_z = 1.0f) const;
	bool nearly_zero(float tolerance = 1e-4f) const;
};

struct Rotator {
	float pitch_deg = 0.0f;
	float yaw_deg = 0.0f;
	float roll_deg = 0.0f;

	DROIDCLI_API Vec3 rotate_vector(const Vec3& local) const;
};

struct ColorRGBA {
	uint8_t r = 255;
	uint8_t g = 255;
	uint8_t b = 255;
	uint8_t a = 255;
};

using String = std::string;
template<typename T>
using Array = std::vector<T>;

} // namespace droidcli::core
