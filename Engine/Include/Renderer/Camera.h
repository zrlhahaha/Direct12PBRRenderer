#pragma once
#include "Renderer/Device/Direct12/D3D12Device.h"
#include "Utils/MathLib.h"

namespace MRenderer 
{
    class Camera 
    {
    public:
        Camera(float fov, uint32 width, uint32 height, float near_plane, float far_plane)
            :mFov(fov), mRatio(static_cast<float>(width) / static_cast<float>(height)),
             mNear(near_plane), mFar(far_plane), mRoll(0), mYaw(0), mPitch(0),
             mViewSpaceTransform(Matrix4x4::Identity())
        {
        }

        inline void Move(const Vector3& delta_pos) 
        {
            mViewSpaceTransform.Translate(delta_pos);
        }

        void Rotate(float roll, float yaw, float pitch);

        inline Matrix4x4 GetWorldMatrix() const { return mViewSpaceTransform;} // view space to world space
        inline Matrix4x4 GetLocalSpaceMatrix() const { return mViewSpaceTransform.QuickInverse();} // world space to view space
        inline Matrix4x4 GetProjectionMatrix() const { return ProjectionMatrix1(mFov, mRatio, mNear, mFar);}
        inline Vector3 GetTranslation() const { return mViewSpaceTransform.GetTranslation(); }
        inline float Near() const { return mNear; }
        inline float Far() const { return mFar; }
        inline float Fov() const { return mFov; }
        inline float Ratio() const { return mRatio; }

    protected:
        float mFov;
        float mRatio;
        float mNear;
        float mFar;

        float mRoll;
        float mYaw;
        float mPitch;

        Matrix4x4 mViewSpaceTransform;
    };
}