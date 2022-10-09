#pragma once

#include "MathHelper.h"

using namespace DirectX;

class alignas(16) Quaternion
{
public:
	Quaternion() { m_vec = XMQuaternionIdentity(); }
	Quaternion(const FXMVECTOR& axis, const float angle) { m_vec = XMQuaternionRotationAxis(axis, angle); }
	Quaternion(float pitch, float yaw, float roll) { m_vec = XMQuaternionRotationRollPitchYaw(pitch, yaw, roll); }
	explicit Quaternion(const XMMATRIX& matrix) { m_vec = XMQuaternionRotationMatrix(matrix); }
	explicit Quaternion(FXMVECTOR vec) { m_vec = vec; }

	XM_CALLCONV operator XMVECTOR() const { return m_vec; }

	Quaternion XM_CALLCONV operator~(void) const { return Quaternion(XMQuaternionConjugate(m_vec)); }
	Quaternion XM_CALLCONV operator-(void) const { return Quaternion(XMVectorNegate(m_vec)); }

	Quaternion XM_CALLCONV operator*(Quaternion rhs) const { return Quaternion(XMQuaternionMultiply(rhs, m_vec)); }
	XMVECTOR XM_CALLCONV operator*(FXMVECTOR rhs) const { return XMVector3Rotate(rhs, m_vec); }

	Quaternion& XM_CALLCONV operator=(Quaternion rhs) {
		m_vec = rhs;
		return *this;
	}
	Quaternion& XM_CALLCONV operator*=(Quaternion rhs) {
		*this = *this * rhs;
		return *this;
	}

private:
	XMVECTOR m_vec;
};


