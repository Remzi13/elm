#include "graphics/Camera.hpp"


#include <algorithm>
#include <cmath>

namespace elm {

	Camera::Camera(const Vector3& position, float yaw, float pitch)
		: m_position(position), m_yaw(yaw), m_pitch(pitch) {
	}

	void Camera::SetFreezeCulling(bool freeze) noexcept {
		if (freeze && !m_freezeCulling) {
			m_frozenCullingViewProj = GetViewProjectionMatrix();
		}
		m_freezeCulling = freeze;
	}

	Matrix4x4 Camera::GetCullingViewProjection() const noexcept {
		if (m_freezeCulling) {
			return m_frozenCullingViewProj;
		}
		return GetViewProjectionMatrix();
	}

	Matrix4x4 Camera::GetViewMatrix() const noexcept {
		const float cosPitch = std::cos(m_pitch);
		const float sinPitch = std::sin(m_pitch);
		const float cosYaw = std::cos(m_yaw);
		const float sinYaw = std::sin(m_yaw);

		const Vector3 forward{ sinYaw * cosPitch, sinPitch, cosYaw * cosPitch };
		return Matrix4x4::LookAt(m_position, m_position + forward, Vector3{ 0.0f, 1.0f, 0.0f });
	}

	Matrix4x4 Camera::GetProjectionMatrix() const noexcept {
		return Matrix4x4::PerspectiveVk(fovRadians, m_aspect, zNear, zFar);
	}

	Matrix4x4 Camera::GetViewProjectionMatrix() const noexcept {
		return GetProjectionMatrix() * GetViewMatrix();
	}

	void Camera::Rotate(float yawDelta, float pitchDelta) noexcept {
		SetOrientation(m_yaw + yawDelta, m_pitch + pitchDelta);
	}

	void Camera::SetOrientation(float yaw, float pitch) noexcept {
		constexpr float maxPitch = 1.55f;
		m_yaw = yaw;
		m_pitch = std::clamp(pitch, -maxPitch, maxPitch);
	}

	Vector3 Camera::GetForwardDirection() const noexcept {
		const float cosPitch = std::cos(m_pitch);
		const float sinPitch = std::sin(m_pitch);
		const float cosYaw = std::cos(m_yaw);
		const float sinYaw = std::sin(m_yaw);
		return { sinYaw * cosPitch, sinPitch, cosYaw * cosPitch };
	}

	Vector3 Camera::GetRightDirection() const noexcept {
		return { std::cos(m_yaw), 0.0f, -std::sin(m_yaw) };
	}

	ViewRay Camera::GetRayThrough(float ndcX, float ndcY) const noexcept {
		const Vector3 forward = GetForwardDirection();
		const Vector3 right = GetRightDirection();
		const Vector3 up = right.Cross(forward).Normalized();
		// How far a unit step along right and up moves in NDC at distance 1: the projection
		// itself gives the scale and the sign, whatever its handedness or field of view
		const Matrix4x4 viewProjection = GetViewProjectionMatrix();
		const auto ndcOf = [&](const Vector3& point) {
			const Vector4 clip = viewProjection.TransformPoint(point);
			return Vector3 { clip.x / clip.w, clip.y / clip.w, 0.0f };
		};
		const float ndcPerRight = ndcOf(m_position + forward + right).x;
		const float ndcPerUp = ndcOf(m_position + forward + up).y;
		if (ndcPerRight == 0.0f || ndcPerUp == 0.0f)
			return { m_position, forward };
		const Vector3 direction = forward + right * (ndcX / ndcPerRight) + up * (ndcY / ndcPerUp);
		return { m_position, direction.Normalized() };
	}

} // namespace Engine
