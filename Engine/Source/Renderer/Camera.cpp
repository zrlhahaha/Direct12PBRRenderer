#include "Renderer/Camera.h"

namespace MRenderer 
{
    void Camera::Rotate(float roll, float yaw, float pitch)
    {
        mRoll += roll;
        mYaw += yaw;
        mPitch += pitch;

        mViewSpaceTransform.SetRotation(Matrix3x3::FromEulerAngle(mRoll, mYaw, mPitch));
    }
}
