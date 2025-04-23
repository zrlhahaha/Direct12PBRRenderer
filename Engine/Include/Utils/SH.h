#pragma once
#include "Fundation.h"
#include "MathLib.h"
#include <array>
#include <random>

namespace MRenderer 
{
    class TextureData;

    template<size_t Order>
    struct SHCoefficients
    {
        static constexpr size_t CoefficientsCount = (Order + 1) * (Order + 1);
        static constexpr size_t SHOrder = Order;

        std::array<float, CoefficientsCount> Data = {};
    };

    struct SH2CoefficientsPack
    {
        Vector4 sha_r; // {x, y, z, 1}
        Vector4 shb_r; // {x*y, y*z, z*z, z*x}
        Vector4 sha_g; // {x, y, z, 1}
        Vector4 shb_g; // {x*y, y*z, z*z, z*x}
        Vector4 sha_b; // {x, y, z, 1}
        Vector4 shb_b; // {x*y, y*z, z*z, z*x}
        Vector4 shc;   // {x*x - y*y, x*x - y*y, x*x - y*y, 0}
    };


    using SH2Coefficients = SHCoefficients<2>;

    static_assert(sizeof(SH2Coefficients) == 9 * sizeof(float));

    // ref:https://3dvar.com/Green2003Spherical.pdf
    class SHBaker 
    {    
    public:
        static constexpr uint32 SampleCount = 100000;

        // SH basis function
        // warn: make sure dir is unit vector, this function won't do normalization for efficiency
        static float SHBasisFunction(int n, Vector3 dir);

        // coefficient part of SH basis function
        static float SHBasisFunctionCoefficient(int n);

        // SH coefficients for cos(theta) function
        static float CosineSHCoefficients(int l);

        // calculate 2 order SH coefficients from cubemap, which can be used for approximate environment irradiance
        static void ProjectEnvironmentMap(const std::array<TextureData, 6>& cube_map, SH2Coefficients& out_sh_r, SH2Coefficients& out_sh_g, SH2Coefficients& out_sh_b);

        // generate irradiance map from cube map
        static std::array<TextureData, 6> GenerateIrradianceMap(const std::array<TextureData, 6>& cube_map, uint32 map_size, bool debug=false);

        // irradiance approximation is a polynomial. this function will merge the polynomial terms as much as possible.
        static SH2CoefficientsPack PackCubeMapSHCoefficient(SH2Coefficients r, SH2Coefficients g, SH2Coefficients b);

        // calculate the irradiance from SH coefficients
        // warn: normal must be unit vector
        static Vector3 CalcIrradiance(const SH2CoefficientsPack& pack, const Vector3& normal);
        static Vector3 CalcIrradiance2(const SH2Coefficients& shr, const SH2Coefficients& shg, const SH2Coefficients& shb, const Vector3& normal);
    };
}