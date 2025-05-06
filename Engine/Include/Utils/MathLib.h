#pragma once
#include "Fundation.h"

#include <sstream>
#include <smmintrin.h>

namespace MRenderer
{
    constexpr float PI = 3.14159265358979323846F;
    constexpr float SqrtPI = 1.7724538509055159F;
    constexpr float InvPI = 1.0F / PI;
    constexpr float Inv255 = 1.0F / 255.0F;
    constexpr float Rad2Deg = 180.0F / PI;
    constexpr float Deg2Rad = PI / 180.0F;

    constexpr float Pow(float base, uint32 n)
    {
        return n == 0 ? 1 : base * Pow(base, n - 1);
    }

    template<typename T>
        requires std::is_arithmetic_v<T>
    constexpr T Clamp(T x, std::type_identity_t<T> a, std::type_identity_t<T> b)
    {
        if (x < a)
        {
            return a;
        }
        else if (x > b)
        {
            return b;
        }
        else
        {
            return x;
        }
    }

    template<typename T>
    constexpr T Min(T a, std::type_identity_t<T> b)
    {
        return a < b ? a : b;
    }

    template<typename T>
    constexpr T Max(T a, std::type_identity_t<T> b)
    {
        return a > b ? a : b;
    }

    // require SSE 4.1
    // see also: https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html#ssetechs=SSE,SSE2,SSE3
    template<uint32 N> struct Vector;
    
    template <uint32 N> requires (2 <= N && N <= 4)
    struct VectorOperation
    {
    public:
        using VecType = Vector<N>;
        static constexpr uint32 VectorSize = N * sizeof(float);

    public:

        inline float& At(uint32 index) 
        {
            ASSERT(index >= 0 && index < N);
            return reinterpret_cast<float*>(this)[index];
        }

        inline const float& At(uint32 index) const
        {
            ASSERT(index >= 0 && index < N);
            return reinterpret_cast<const float*>(this)[index];
        }

        float Length() const 
        {
            const float* m = reinterpret_cast<const float*>(this);

            float sum = 0;
            for (uint32 i = 0; i < N; i++) 
            {
                sum += m[i] * m[i];
            }

            return sqrt(sum);
        }

        void Normalize() 
        {
            float* m = reinterpret_cast<float*>(this);
            float length = Length();

            for (uint32 i = 0; i < N; i++) 
            {
                m[i] /= length;
            }
        }

        VecType GetNormalized() const
        {
            const VecType* vec = reinterpret_cast<const VecType*>(this);
            VecType ret = *vec;
            ret.Normalize();
            return ret;
        }

        float Dot(const VecType& other) const
        {
            __m128 lhs = Store();
            __m128 rhs = other.Store();

            // for vector2 - imm8 = 0x31
            // for vecotr3 - imm8 = 0x71
            // for vector4 - imm8 = 0xF1
            // e.g 0x71 means dot product the first 3 elements and store the result in the first element of the returened __m128
            constexpr uint32 imm8 = (((1 << N) - 1) << 4) | 0x1;
            lhs = _mm_dp_ps(lhs, rhs, imm8);

            float* staging = StagingBuffer();
            _mm_store_ps(staging, lhs);
            return staging[0];
        }

        std::string ToString() const
        {
            const float* m = reinterpret_cast<const float*>(this);

            std::stringstream ss;
            ss << "[";
            for (uint32 i = 0; i < N; i++) 
            {
                ss << m[i];
                if (i != N - 1) 
                {
                    ss << ",";
                }
            }
            ss << "]";
            return ss.str();
        }

        VecType operator* (float scale) const
        {
            __m128 lhs = Store();
            __m128 rhs = _mm_set1_ps(scale);
            lhs = _mm_mul_ps(lhs, rhs);

            VecType ret;
            ret.Load(lhs);
            return ret;
        }

        VecType operator/ (float scale) const
        {
            __m128 lhs = Store();
            __m128 rhs = _mm_set1_ps(scale);
            lhs = _mm_div_ps(lhs, rhs);

            VecType ret;
            ret.Load(lhs);
            return ret;
        }

        VecType operator+ (const VecType& other) const
        {
            __m128 lhs = Store();
            __m128 rhs = other.Store();
            lhs = _mm_add_ps(lhs, rhs);

            VecType ret;
            ret.Load(lhs);
            return ret;
        }

        VecType operator- (const VecType& other) const
        {
            __m128 lhs = Store();
            __m128 rhs = other.Store();
            lhs = _mm_sub_ps(lhs, rhs);

            VecType ret;
            ret.Load(lhs);
            return ret;
        }

        bool operator==(const VecType& other) const 
        {
            __m128 lhs = Store();
            __m128 rhs = other.Store();
            lhs = _mm_cmpeq_ps(lhs, rhs);
            
            VecType ret;
            ret.Load(lhs);

            for (uint32 i = 0; i < N; i++) 
            {
                if (!ret.At(i)) 
                {
                    return false;
                }
            }

            return true;
        }

        bool operator<(const VecType& other)
        {
            float* m = reinterpret_cast<float*>(this);
            float* n = reinterpret_cast<float*>(other);

            for(uint32 i = 0; i < N; i++)
            {
                if (m[i] < n[i])
                {
                    return true;
                }
            }
            return false;
        }

        float& operator[] (uint32 index)
        {
            return At(index);
        }

        const float& operator[] (uint32 index) const
        {
            return At(index);
        }


        // copy this vector to a __m128
        inline __m128 Store() const
        {
            // The size and alignment of Vector2 and Vector3 are not 16 bytes.
            // They need a 16-byte aligned staging buffer to create __m128.
            if constexpr (N != 4)
            {
                float* staging = StagingBuffer();
                memcpy(staging, this, VectorSize);
                return _mm_load_ps(staging);
            }
            // Vector4's size and alignment is 16 bytes, it can creat __m128 directly
            else 
            {
                const float* m = reinterpret_cast<const float*>(this);
                return _mm_load_ps(m);
            }
        }

        // copy src __m128 value to this vector
        inline void Load(__m128 src)
        {
            if constexpr (N != 4)
            {
                float* staging = StagingBuffer();
                _mm_store_ps(staging, src);
                memcpy(this, staging, VectorSize);
            }
            else 
            {
                float* m = reinterpret_cast<float*>(this);
                _mm_store_ps(m, src);
            }
        }

    public:
        static VecType Clamp(const VecType& vec, const VecType& min, const VecType& max)
        {
            VecType ret;
            for (uint32 i = 0; i < N; i++)
            {
                ret[i] = MRenderer::Clamp(vec.At(i), min.At(i), max.At(i));
            }
            return ret;
        }

        static VecType Min(const VecType& a, const VecType& b) 
        {
            VecType ret;
            for (uint32 i = 0; i < N; i++)
            {
                ret[i] = MRenderer::Min(a.At(i), b.At(i));
            }
            return ret;
        }

        static VecType Max(const VecType& a, const VecType& b)
        {
            VecType ret;
            for (uint32 i = 0; i < N; i++)
            {
                ret[i] = MRenderer::Max(a.At(i), b.At(i));
            }
            return ret;
        }

    protected:
        // 16 byte aligned float[4] staging buffer, it's used as input for _mm_load_ps and output for _mm_store_ps
        // since they require input or output to be 16 byte aligned
        static float* StagingBuffer()
        {
            static thread_local struct alignas(alignof(__m128)) {
                float m[4];
            } staging;

            return staging.m;
        }
    };

    template<>
    struct Vector <2> : VectorOperation<2>
    {
    public:
        Vector() 
        {
            memset(this, 0, VectorSize);
        }

        Vector(float x, float y)
            :x(x), y(y)
        {
        }


    public:
        float x, y;
    };

    template<>
    struct Vector <3> : VectorOperation<3>
    {
    public:
        Vector()
        {
            memset(this, 0, VectorSize);
        }

        Vector(float x, float y, float z)
            :x(x), y(y), z(z)
        {
        }

        Vector(const Vector<4>& vec) 
        {
            memcpy(this, &vec, VectorSize);
        }

        Vector(const Vector<2>& vec, float z) 
            : z(z)
        {
            memcpy(this, &vec, vec.VectorSize);
        }

    public:
        float x, y, z;
    };

    template<>
    struct alignas(16) Vector <4> : VectorOperation<4>
    {
    public:
        Vector()
        {
            memset(this, 0, VectorSize);
        }

        Vector(float x, float y, float z, float w)
            :x(x), y(y), z(z), w(w)
        {
        }

        Vector(const Vector<3>& vec, float w)
            :w(w)
        {
            memcpy(this, &vec, Vector<3>::VectorSize);
        }

        Vector(const Vector<2>& vec, float z, float w)
            :z(z), w(w)
        {
            memcpy(this, &vec, Vector<3>::VectorSize);
        }

    public:
        static inline Vector Zero() 
        {
            return Vector(0, 0, 0, 0);
        }


    public:
        float x, y, z, w;
    };

    using Vector2 = Vector<2>;
    using Vector3 = Vector<3>;
    using Vector4 = Vector<4>;

    static_assert(sizeof(Vector2) == 2 * sizeof(float));
    static_assert(sizeof(Vector3) == 3 * sizeof(float));
    static_assert(sizeof(Vector4) == 4 * sizeof(float));


    template<typename MatrixType, uint32 Row, uint32 Column> requires(Row == Column)
    struct MatrixOperation 
    {
    protected:
        using VectorType = Vector<Column>;

        static constexpr uint32 RowSize = Row * sizeof(float);
        // for matrix3x3 - imm8 = 0x71
        // for matrix4x4 - imm8 = 0xF1
        // e.g 0x71 means dot product the first 3 elements and store the result in the first element of the returened __m128
        static constexpr uint32 imm8 = (((1 << Row) - 1) << 4) | 0x1;

    public:
        inline float& At(uint32 row, uint32 column)
        {
            ASSERT(row < Row && column < Column);

            float* m = reinterpret_cast<float*>(this);
            return m[row * Row + column];
        }

        inline const float& At(uint32 row, uint32 column) const
        {
            const float* m = reinterpret_cast<const float*>(this);
            return m[row * Row + column];
        }

        inline Vector<Row> GetRow(uint32 row) const
        { 
            ASSERT(row < Row);

            Vector<Row> ret;
            const float* m = reinterpret_cast<const float*>(this);

            for (uint32 i = 0; i < Row; i++)
            {
                ret[i] = m[row * Row + i];
            }
            return ret;
        }

        inline Vector<Column> GetColumn(uint32 column) const
        {
            ASSERT(column < Column);
            Vector<Column> ret;
            const float* m = reinterpret_cast<const float*>(this);
            for (uint32 i = 0; i < Column; i++)
            {
                ret[i] = m[i * Row + column];
            }
            return ret;
        }

        inline VectorType operator* (const VectorType& other) const
        {
            VectorType ret;

            for (uint32 r = 0; r < Row; r++)
            {
                __m128 lhs = StoreRow(r);
                __m128 rhs = other.Store();

                lhs = _mm_dp_ps(lhs, rhs, imm8);

                float* staging = StagingBuffer();
                _mm_store_ps(staging, lhs);

                ret.At(r) = staging[0];
            }
            return ret;
        }

        MatrixType operator* (const MatrixType& other) const
        {
            MatrixType ret;

            for (uint32 r = 0; r < Row; r++)
            {
                for (uint32 c = 0; c < Column; c++)
                {
                    __m128 lhs = StoreRow(r);
                    __m128 rhs = other.StoreColumn(c);

                    lhs = _mm_dp_ps(lhs, rhs, imm8);

                    float* staging = StagingBuffer();
                    _mm_store_ps(staging, lhs);

                    ret.At(r, c) = staging[0];
                }
            }

            return ret;
        }

        float& operator() (uint32 row, uint32 col)
        {
            return At(row, col);
        }

    protected:
        __m128 StoreRow(uint32 row) const
        {
            ASSERT(row >= 0 && row < Row);
            const float* m = reinterpret_cast<const float*>(this);

            if constexpr (Row != 4)
            {
                float* staging = StagingBuffer();
                memcpy(staging, m + row * Row, RowSize);

                return _mm_load_ps(staging);
            }
            else 
            {
                const float* row_base = m + row * Row;
                return _mm_load_ps(row_base);
            }

        }

        __m128 StoreColumn(uint32 column) const
        {
            ASSERT(column >= 0 && column < Column);
            const float* m = reinterpret_cast<const float*>(this);

            float* staging = StagingBuffer();
            for (uint32 i = 0; i < Row; i++)
            {
                staging[i] = *(m + i * Row + column);
            }

            return _mm_load_ps(staging);
        }

        float* StagingBuffer() const
        {
            // 16 bytes aligned staging buffer for load/store __m128
            static thread_local struct alignas(alignof(__m128)) {
                float m[4];
            } staging;

            return staging.m;
        }
    };



    // note: 
    // Matrix3x3 and Matrix4x4 are row major
    struct Matrix3x3 : MatrixOperation<Matrix3x3, 3, 3>
    {
    protected:
        static constexpr uint32 Row = 3;
        static constexpr uint32 Column = 3;

    public:
        Matrix3x3()
            : m{}
        {
        }

        Matrix3x3(float m00, float m01, float m02, float m10, float m11, float m12, float m20, float m21, float m22)
            : m{ m00, m01, m02, m10, m11, m12, m20, m21, m22 }
        {
        }

        void Transpose()
        {
            Matrix3x3 temp = 
            {
                m[0], m[3], m[6],
                m[1], m[4], m[7],
                m[2], m[5], m[8]
            };

            memcpy(this, &temp, sizeof(Matrix3x3));
        }

    public:
        static Matrix3x3 Identity()
        {
            return Matrix3x3(
                1, 0, 0,
                0, 1, 0,
                0, 0, 1
            );
        }

        static Matrix3x3 FromEulerAngle(float yaw, float pitch, float roll)
        {
            float ca = cos(yaw);
            float sa = sin(yaw);
            float cb = cos(pitch);
            float sb = sin(pitch);
            float cc = cos(roll);
            float sc = sin(roll);

            return Matrix3x3(
                ca * cb, ca * sb * sc - sa * cc, ca * sb * cc + sa * sc,
                sa * cb, sa * sb * sc + ca * cc, sa * sb * cc - ca * sc,
                -sb, cb * sc, cb * cc
            );
        }

        Vector3 GetEulerAngle()
        {
            float pitch = asin(-At(2, 0));
            float roll = atan2(At(1, 0), At(0, 0));
            float yaw = atan2(At(2, 1), At(2, 2));

            return Vector3(yaw, pitch, roll);
        }

    protected:
        float m[9];
    };

    static_assert(sizeof(Matrix3x3) == 9 * sizeof(float));

    struct alignas(16)  Matrix4x4 : MatrixOperation<Matrix4x4, 4, 4>
    {
    protected:
        static constexpr uint32 Row = 3;
        static constexpr uint32 Column = 3;
        static constexpr uint32 RowSize = Row * sizeof(float);

    public:
        Matrix4x4()
            :m{}
        {
        }

        Matrix4x4(
            float m00, float m01, float m02, float m03,
            float m10, float m11, float m12, float m13,
            float m20, float m21, float m22, float m23,
            float m30, float m31, float m32, float m33
        )
            :m{ m00, m01, m02, m03, m10, m11, m12, m13, m20, m21, m22, m23, m30, m31, m32, m33 }
        {
        }

        void Translate(const Vector3& vec) 
        {
            m[3] += vec.x;
            m[7] += vec.y;
            m[11] += vec.z;
        }

        Vector3 GetTranslation() const
        {
            return Vector3(m[3], m[7], m[11]);
        }

        Vector3 GetScale() const
        {
            Vector3 x = { m[0] , m[4], m[8]  };
            Vector3 y = { m[1] , m[5], m[9]  };
            Vector3 z = { m[2] , m[6], m[10] };

            return Vector3{ x.Length(), y.Length(), z.Length() };
        }

        Matrix3x3 GetRotation() const 
        {
            Vector3 scale = GetScale();

            return Matrix3x3(
                m[0] / scale.x, m[1] / scale.y, m[2]  / scale.z,
                m[4] / scale.x, m[5] / scale.y, m[6]  / scale.z,
                m[8] / scale.x, m[9] / scale.y, m[10] / scale.z
            );
        }

        void SetRotation(const Matrix3x3 rotation)
        {
            Vector3 scale = GetScale();

            At(0, 0) = rotation.At(0, 0) * scale.x;
            At(0, 1) = rotation.At(0, 1) * scale.y;
            At(0, 2) = rotation.At(0, 2) * scale.z;
            At(1, 0) = rotation.At(1, 0) * scale.x;
            At(1, 1) = rotation.At(1, 1) * scale.y;
            At(1, 2) = rotation.At(1, 2) * scale.z;
            At(2, 0) = rotation.At(2, 0) * scale.x;
            At(2, 1) = rotation.At(2, 1) * scale.y;
            At(2, 2) = rotation.At(2, 2) * scale.z;
        }

        void SetRotation(float roll, float yaw, float pitch)
        {
            SetRotation(Matrix3x3::FromEulerAngle(roll, yaw, pitch));
        }

        void SetTranslation(const Vector3& translation) 
        {
            m[3] = translation.x;
            m[7] = translation.y;
            m[11] = translation.z;
        }

        void SetScale(const Vector3& scale) 
        {
            Vector3 x = { m[0] , m[4], m[8] };
            Vector3 y = { m[1] , m[5], m[9] };
            Vector3 z = { m[2] , m[6], m[10] };

            x = x.GetNormalized() * scale.x;
            y = y.GetNormalized() * scale.y;
            z = z.GetNormalized() * scale.z;

            m[0] = x.x; m[4] = x.y; m[8] = x.z;
            m[1] = y.x; m[5] = y.y; m[9] = y.z;
            m[2] = z.x; m[6] = z.y; m[10] = z.z;
        }

        // only valid for the combination of rotation, scale and translation matrix
        // for something like projection matrix, use Inverse instead
        Matrix4x4 QuickInverse() const
        {
            // M = R * S
            // P` = MP + T
            // P = M^-1 * P` - M^-1 * T
            Matrix3x3 rotation = GetRotation();
            rotation.Transpose();

            Vector3 scale = GetScale();
            scale = { 1.0f / scale.x, 1.0f / scale.y, 1.0f / scale.z };

            Matrix3x3 inv_m = {
                rotation.At(0, 0) * scale.x, rotation.At(0, 1) * scale.y, rotation.At(0, 2) * scale.z,
                rotation.At(1, 0) * scale.x, rotation.At(1, 1) * scale.y, rotation.At(1, 2) * scale.z,
                rotation.At(2, 0) * scale.x, rotation.At(2, 1) * scale.y, rotation.At(2, 2) * scale.z
            };

            Vector3 inv_translation = inv_m * Vector3(m[3], m[7], m[11]);

            return Matrix4x4(
                inv_m.At(0, 0), inv_m.At(0, 1), inv_m.At(0, 2), -inv_translation.x,
                inv_m.At(1, 0), inv_m.At(1, 1), inv_m.At(1, 2), -inv_translation.y,
                inv_m.At(2, 0), inv_m.At(2, 1), inv_m.At(2, 2), -inv_translation.z,
                0, 0, 0, 1
            );
        }

        Matrix4x4 Inverse() const 
        {
            Matrix4x4 ret = Matrix4x4::Identity();
            float inv[16], det;
            int i;

            inv[0] = m[5] * m[10] * m[15] -
                m[5] * m[11] * m[14] -
                m[9] * m[6] * m[15] +
                m[9] * m[7] * m[14] +
                m[13] * m[6] * m[11] -
                m[13] * m[7] * m[10];

            inv[4] = -m[4] * m[10] * m[15] +
                m[4] * m[11] * m[14] +
                m[8] * m[6] * m[15] -
                m[8] * m[7] * m[14] -
                m[12] * m[6] * m[11] +
                m[12] * m[7] * m[10];

            inv[8] = m[4] * m[9] * m[15] -
                m[4] * m[11] * m[13] -
                m[8] * m[5] * m[15] +
                m[8] * m[7] * m[13] +
                m[12] * m[5] * m[11] -
                m[12] * m[7] * m[9];

            inv[12] = -m[4] * m[9] * m[14] +
                m[4] * m[10] * m[13] +
                m[8] * m[5] * m[14] -
                m[8] * m[6] * m[13] -
                m[12] * m[5] * m[10] +
                m[12] * m[6] * m[9];

            inv[1] = -m[1] * m[10] * m[15] +
                m[1] * m[11] * m[14] +
                m[9] * m[2] * m[15] -
                m[9] * m[3] * m[14] -
                m[13] * m[2] * m[11] +
                m[13] * m[3] * m[10];

            inv[5] = m[0] * m[10] * m[15] -
                m[0] * m[11] * m[14] -
                m[8] * m[2] * m[15] +
                m[8] * m[3] * m[14] +
                m[12] * m[2] * m[11] -
                m[12] * m[3] * m[10];

            inv[9] = -m[0] * m[9] * m[15] +
                m[0] * m[11] * m[13] +
                m[8] * m[1] * m[15] -
                m[8] * m[3] * m[13] -
                m[12] * m[1] * m[11] +
                m[12] * m[3] * m[9];

            inv[13] = m[0] * m[9] * m[14] -
                m[0] * m[10] * m[13] -
                m[8] * m[1] * m[14] +
                m[8] * m[2] * m[13] +
                m[12] * m[1] * m[10] -
                m[12] * m[2] * m[9];

            inv[2] = m[1] * m[6] * m[15] -
                m[1] * m[7] * m[14] -
                m[5] * m[2] * m[15] +
                m[5] * m[3] * m[14] +
                m[13] * m[2] * m[7] -
                m[13] * m[3] * m[6];

            inv[6] = -m[0] * m[6] * m[15] +
                m[0] * m[7] * m[14] +
                m[4] * m[2] * m[15] -
                m[4] * m[3] * m[14] -
                m[12] * m[2] * m[7] +
                m[12] * m[3] * m[6];

            inv[10] = m[0] * m[5] * m[15] -
                m[0] * m[7] * m[13] -
                m[4] * m[1] * m[15] +
                m[4] * m[3] * m[13] +
                m[12] * m[1] * m[7] -
                m[12] * m[3] * m[5];

            inv[14] = -m[0] * m[5] * m[14] +
                m[0] * m[6] * m[13] +
                m[4] * m[1] * m[14] -
                m[4] * m[2] * m[13] -
                m[12] * m[1] * m[6] +
                m[12] * m[2] * m[5];

            inv[3] = -m[1] * m[6] * m[11] +
                m[1] * m[7] * m[10] +
                m[5] * m[2] * m[11] -
                m[5] * m[3] * m[10] -
                m[9] * m[2] * m[7] +
                m[9] * m[3] * m[6];

            inv[7] = m[0] * m[6] * m[11] -
                m[0] * m[7] * m[10] -
                m[4] * m[2] * m[11] +
                m[4] * m[3] * m[10] +
                m[8] * m[2] * m[7] -
                m[8] * m[3] * m[6];

            inv[11] = -m[0] * m[5] * m[11] +
                m[0] * m[7] * m[9] +
                m[4] * m[1] * m[11] -
                m[4] * m[3] * m[9] -
                m[8] * m[1] * m[7] +
                m[8] * m[3] * m[5];

            inv[15] = m[0] * m[5] * m[10] -
                m[0] * m[6] * m[9] -
                m[4] * m[1] * m[10] +
                m[4] * m[2] * m[9] +
                m[8] * m[1] * m[6] -
                m[8] * m[2] * m[5];

            det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];

            if (det == 0)
                return ret;

            float inv_det = 1.0F / det;

            for (i = 0; i < 16; i++)
                ret.m[i] = inv[i] * inv_det;

            return ret;
        }

    public:
        static Matrix4x4 Identity()
        {
            return Matrix4x4(
                1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1
            );
        }

    protected:
        float m[16];
    };

    static_assert(sizeof(Matrix4x4) == 16 * sizeof(float));
    static_assert(alignof(Matrix4x4) == 4 * sizeof(float)); //  to make sure the optimization in MatrixOperation::StoreRow works

    struct AABB
    {
    public:
        AABB() = default;

        AABB(Vector3 min, Vector3 max)
            : Min(min), Max(max)
        {
        }

        inline float Width() const
        {
            return Max.x - Min.x;
        }

        inline float Height() const
        {
            return Max.y - Min.y;
        }

        inline float Depth() const
        {
            return Max.z - Min.z;
        }

        inline Vector3 Size() const
        {
            return Max - Min;
        }

        inline float Volume() const
        {
            Vector3 size = Size();
            return size.x * size.y * size.z;
        }

        inline Vector3 Center() const
        {
            return (Min + Max) * 0.5f;
        }

        inline bool Contain(const AABB& aabb) const
        {
            return aabb.Min.x > Min.x && aabb.Min.y > Min.y && aabb.Min.z > Min.z &&
                aabb.Max.x < Max.x && aabb.Max.y < Max.y && aabb.Max.z < Max.z;
        }

    public:
        Vector3 Min;
        Vector3 Max;
    };

    AABB operator* (const Matrix4x4& mat, const AABB& aabb);

    struct FrustumVolume
    {
        // plane function dot(N, P) + D = 0, when P satisfy all plane function evaluates above 0, it's inside the Frustum
        // each Vector4 is composed by (N.x, N.y, N.z, D)
        // Planes contains 6 Frustum planes: Left, Right, Bottom, Top, Near, Far
        Vector4 Planes[6];

        // ref: https://zhuanlan.zhihu.com/p/573372287
        static FrustumVolume FromMatrix(const Matrix4x4& matrix)
        {
            FrustumVolume volume{};

            Vector4 row0 = matrix.GetRow(0);
            Vector4 row1 = matrix.GetRow(1);
            Vector4 row2 = matrix.GetRow(2);
            Vector4 row3 = matrix.GetRow(3);

            volume.Planes[0] = row3 + row0;
            volume.Planes[1] = row3 - row0;
            volume.Planes[2] = row3 + row1;
            volume.Planes[3] = row3 - row1;
            volume.Planes[4] = row3 + row2;
            volume.Planes[5] = row3 - row2;

            return volume;
        }

        bool Contains(const Vector3& point) const
        {
            for (uint32 i = 0; i < 6; i++)
            {
                // all plane function evaluates above 0 means the point is inside the Frustum, otherwise it's outside
                if (Planes[i].Dot(Vector4(point, 1)) < 0)
                {
                    return false;
                }
            }

            return true;
        }

        bool Contains(const AABB& bound) const 
        {
            Vector3 center = bound.Center(); 
            Vector3 extent = bound.Size() * 0.5f;
            for (uint32 i = 0; i < 6; i++)
            {
                const Vector4 plane = Planes[i];

                // ref: https://gdbooks.gitbooks.io/3dcollisions/content/Chapter2/static_aabb_plane.html
                // there are eight possible diagnoal vector, we choose the one thas has the largest projection distance
                float half_diagnoal_projection = abs(plane.x * extent.x) + abs(plane.y * extent.y) + abs(plane.z * extent.z);
                float center_distance = plane.Dot(Vector4(center, 1));

                // AABB are above the plane if the half-length of its diagnoal projected onto the plane's normal is greater than the distance from center to the plane
                // @center_distance > @half_diagnoal_projection => AABB above the plane
                // @center_distance < -@half_diagnoal_projection => AABB below the plane, i.e it's outside of the frustum
                // @center_distance >= -@half_diagnoal_projection & @center_distance <= @half_diagnoal_projection => AABB intersect with the plane
                if (center_distance < -half_diagnoal_projection) 
                {
                    return false;
                }
            }
            return true;
        }
    };

    struct FrustumCullStatus
    {
        uint32 NumDrawCall;
        uint32 NumCulled;
    };

    template<uint32 N>
    void LogImpl(Vector<N> vec)
    {
        std::stringstream ss;
        ss << "<";
        for (uint32 i = 0; i < N; i++) 
        {
            vec.At(i);
            if (i != N - 1) 
            {
                ss << ", ";
            }
            ss << ">";
        }

        std::cout << ss.str();
    }

    // ndc.z -> [-1, 1]
    Matrix4x4 ProjectionMatrix0(float fov, float ratio, float near_z, float far_z);

    // ndc.z -> [0, 1]
    Matrix4x4 ProjectionMatrix1(float fov, float ratio, float near_z, float far_z);

    // spherical coordinates to position on unit sphere
    inline Vector3 FromSphericalCoordinate(float theta, float phi) 
    {
        float sin_theta = sin(theta);
        return Vector3(sin_theta * cos(phi), sin_theta * sin(phi), cos(theta));
    }

    // calculate the texture coordinate of a point pointed by @dir on a cube map.
    void CalcCubeMapCoordinate(Vector3 dir, uint32& out_index, Vector2& out_tc);

    // calculate the direction of a cubemap point which is represented by @index(slice index), @u, @v (uv coordinate)
    Vector3 CalcCubeMapDirection(uint32 index, float u, float v);
}
