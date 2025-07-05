#include "Utils/MathLib.h"

namespace MRenderer
{
    AABB operator*(const Matrix4x4& mat, const AABB& aabb)
    {
        Vector3 min = mat * Vector4(aabb.Min, 1);
        Vector3 max = mat * Vector4(aabb.Max, 1);
        return AABB(Vector3::Min(min, max), Vector3::Max(min, max));
    }
    // ndc.z -> [-1, 1]
    Matrix4x4 ProjectionMatrix0(float fov, float ratio, float near_z, float far_z)
    {
        float l, r, t, b;
        float htan = std::tan(fov * 0.5F);

        r = near_z * ratio * htan;
        l = -r;
        t = near_z * htan;
        b = -t;

        Matrix4x4 ret;
        ret(0, 0) = (2 * near_z) / (r - l);
        ret(0, 2) = (r + l) / (l - r);
        ret(1, 1) = (2 * near_z) / (t - b);
        ret(1, 2) = (t + b) / (b - t);
        ret(2, 2) = (near_z + far_z) / (far_z - near_z);
        ret(2, 3) = (2 * near_z * far_z) / (near_z - far_z);
        ret(3, 2) = 1;

        return ret;
    }

    // ndc.z -> [0, 1]
    Matrix4x4 ProjectionMatrix1(float fov, float ratio, float near_z, float far_z)
    {
        // it's a varriant I derived from opengl projection matrix, it's kind of redundant cause l + r = 0, l - r = w
        // after elimilate these two equations, it's basically the same as XMMatrixPerspectiveFovLH

        float l, r, t, b;
        float htan = std::tan(fov * 0.5F);

        r = near_z * ratio * htan;
        l = -r;
        t = near_z * htan;
        b = -t;

        Matrix4x4 ret;
        ret(0, 0) = (2 * near_z) / (r - l);
        ret(0, 2) = (r + l) / (l - r);
        ret(1, 1) = (2 * near_z) / (t - b);
        ret(1, 2) = (t + b) / (b - t);
        ret(2, 2) = far_z / (far_z - near_z);
        ret(2, 3) = (near_z * far_z) / (near_z - far_z);
        ret(3, 2) = 1;

        auto row0 = ret.GetRow(0);
        auto row1 = ret.GetRow(1);
        auto row2 = ret.GetRow(2);
        auto row3 = ret.GetRow(3);

        auto col0 = ret.GetColumn(0);
        auto col1 = ret.GetColumn(1);
        auto col2 = ret.GetColumn(2);
        auto col3 = ret.GetColumn(3);

        return ret;
    }

    // assume it's in the left hand coordinate system
    // cubemap coordinate system is defined same as direct3d does
    // https://learn.microsoft.com/en-us/windows/win32/direct3d9/cubic-environment-mapping
    void CalcCubeMapCoordinate(Vector3 dir, uint32& out_index, Vector2& out_tc)
    {
        dir.Normalize();

        float abs_x = std::abs(dir.x), abs_y = std::abs(dir.y), abs_z = std::abs(dir.z);

        if (abs_x > abs_y && abs_x > abs_z) 
        {
            // uv basis axis may not in the same direction as the cube basis axis
            // so in some cases, we need to flip the uv coordinate
            if (dir.x > 0) 
            {
                // +X
                out_tc.x = -dir.z / abs_x;
                out_tc.y = -dir.y / abs_x;
                out_index = 0;
            }
            else 
            {
                // -X
                out_tc.x = dir.z / abs_x;
                out_tc.y = -dir.y / abs_x;
                out_index = 1;
            }
        }
        else if (abs_y > abs_x && abs_y > abs_z) 
        {
            if (dir.y > 0) 
            {
                // +Y
                out_tc.x = dir.x / abs_y;
                out_tc.y = dir.z / abs_y;
                out_index = 2;
            }
            else 
            {
                // -Y
                out_tc.x = dir.x / abs_y;
                out_tc.y = -dir.z / abs_y;
                out_index = 3;
            }
        }
        else if (abs_z > abs_x && abs_z > abs_y) 
        {
            if (dir.z > 0) 
            {
                // +Z
                out_tc.x = dir.x / abs_z;
                out_tc.y = -dir.y / abs_z;
                out_index = 4;
            }
            else 
            {
                // -Z
                out_tc.x = -dir.x / abs_z;
                out_tc.y = -dir.y / abs_z;
                out_index = 5;
            }
        }

        // scale tc from [1,-1] to [0,1]
        out_tc.x = (out_tc.x + 1) * 0.5f;
        out_tc.y = (out_tc.y + 1) * 0.5f;
    }

    Vector3 CalcCubeMapDirection(uint32 index, float u, float v)
    {
        // +x, -x, +y, -y, +z, -z
        switch (index)
        {
            case 0:
                return Vector3(1, -v, -u).GetNormalized();
            case 1:
                return Vector3(-1, -v, u).GetNormalized();
            case 2:
                return Vector3(u, 1, v).GetNormalized();
            case 3:
                return Vector3(u, -1, -v).GetNormalized();
            case 4:
                return Vector3(u, -v, 1).GetNormalized();
            case 5:
                return Vector3(-u, -v, -1).GetNormalized();
        }

        UNEXPECTED("unexpected index");
        return Vector3();
    }
}
