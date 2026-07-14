#include "core/types.hpp"

#include <cmath>

namespace droidcli::core {

Vec3& Vec3::operator+=(const Vec3& other)
{
	x += other.x;
	y += other.y;
	z += other.z;
	return *this;
}

Vec3& Vec3::operator-=(const Vec3& other)
{
	x -= other.x;
	y -= other.y;
	z -= other.z;
	return *this;
}

float Vec3::dot(const Vec3& other) const
{
	return x * other.x + y * other.y + z * other.z;
}

float Vec3::length_squared() const
{
	return dot(*this);
}

float Vec3::length() const
{
	return std::sqrt(length_squared());
}

Vec3 Vec3::normalized(float fallback_x, float fallback_y, float fallback_z) const
{
	const float len = length();
	if (len <= 1e-4f)
	{
		return {fallback_x, fallback_y, fallback_z};
	}
	return {x / len, y / len, z / len};
}

bool Vec3::nearly_zero(float tolerance) const
{
	return length_squared() <= tolerance * tolerance;
}

Vec3 Rotator::rotate_vector(const Vec3& local) const
{
	const float pitch = pitch_deg * (3.14159265358979323846f / 180.0f);
	const float yaw = yaw_deg * (3.14159265358979323846f / 180.0f);
	const float roll = roll_deg * (3.14159265358979323846f / 180.0f);

	const float sp = std::sin(pitch);
	const float cp = std::cos(pitch);
	const float sy = std::sin(yaw);
	const float cy = std::cos(yaw);
	const float sr = std::sin(roll);
	const float cr = std::cos(roll);

	const float m00 = cy * cp;
	const float m01 = cy * sp * sr - sy * cr;
	const float m02 = cy * sp * cr + sy * sr;
	const float m10 = sy * cp;
	const float m11 = sy * sp * sr + cy * cr;
	const float m12 = sy * sp * cr - cy * sr;
	const float m20 = -sp;
	const float m21 = cp * sr;
	const float m22 = cp * cr;

	return {
		local.x * m00 + local.y * m01 + local.z * m02,
		local.x * m10 + local.y * m11 + local.z * m12,
		local.x * m20 + local.y * m21 + local.z * m22};
}

} // namespace droidcli::core
