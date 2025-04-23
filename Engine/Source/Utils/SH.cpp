#include "Utils/SH.h"
#include "Resource/ResourceDef.h"

namespace MRenderer
{
    float SHBaker::SHBasisFunction(int n, Vector3 dir)
    {
        // hard code 2 Order SH basis function
        // defination from https://en.wikipedia.org/wiki/Table_of_spherical_harmonics#Real_spherical_harmonics
        ASSERT(n < 16 && n >=0);

        switch (n) 
        {
        case 0:
            return 0.282095f;

        case 1:
            return 0.488603f * dir.y;
        case 2:
            return 0.488603f * dir.z;
        case 3:
            return 0.488603f * dir.x;

        case 4:
            return 1.092548f * dir.x * dir.y;
        case 5:
            return 1.092548f * dir.y * dir.z;
        case 6:
            return 0.315392f * (3 * dir.z * dir.z - 1);
        case 7:
            return 1.092548f * dir.x * dir.z;
        case 8:
            return 0.546274f * (dir.x * dir.x - dir.y * dir.y);
        }

        return 0.0f;
    }

    float SHBaker::SHBasisFunctionCoefficient(int n)
    {
        ASSERT(n < 16 && n >= 0);

        switch (n)
        {
        case 0:
            return 0.282095f;

        case 1:
            return 0.488603f;
        case 2:
            return 0.488603f;
        case 3:
            return 0.488603f;

        case 4:
            return 1.092548f;
        case 5:
            return 1.092548f;
        case 6:
            return 0.315392f;
        case 7:
            return 1.092548f;
        case 8:
            return 0.546274f;
        }

        return 0.0f;
    }

    float SHBaker::CosineSHCoefficients(int l)
    {
        // return SH coefficient for max(cos(theta), 0) function at Y(l, 0)
        // cosine is a symmetric function, which means for arbitrary band, only m = 0 may have non-zero value
        switch (l)
        {
        case 0:
            return SqrtPI / 2.0f;
        case 1:
            return sqrt(PI / 3.0f);
        case 2:
            return sqrt(5.0f * PI) / 8.0f;
        default:
            return 0.0f;
        }
    }

    void SHBaker::ProjectEnvironmentMap(const std::array<TextureData, 6>& cube_map, SH2Coefficients& out_sh_r, SH2Coefficients& out_sh_g, SH2Coefficients& out_sh_b)
    {
        out_sh_r = out_sh_g = out_sh_b = {};
        SH2Coefficients* coeffs[3] = { &out_sh_r, &out_sh_g, &out_sh_b };

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> rng(0.0, 1.0);

        // project RGB channels to SH basis functions
        const uint32 ChannelCount = 3;
        for (uint32 channel_index = 0; channel_index < ChannelCount; channel_index++)
        {
            SH2Coefficients& c = *coeffs[channel_index];

            // calculate radiance SH coefficients
            // we need to solve the integral of f(w) * Y(n) for each SH basis function
            // we will use Monte Carlo integration and uniform PDF as importance sampling method to approximate this integral
            // ref: https://zhuanlan.zhihu.com/p/205664052
            for (uint32 i = 0; i < SampleCount; i++)
            {
                // uniform sample on the unit sphere
                // PDF(theta) = 0.5 * sin(theta)
                // PDF(phi) = 1 / 2PI
                // we will use inverse sampling method on these two PDF to generate uniformly distributed samples
                // ref: http://www.bogotobogo.com/Algorithms/uniform_distribution_sphere.php
                float phi = 2 * PI * rng(gen);
                float theta = acosf(1 - 2 * rng(gen));
                Vector3 dir = FromSphericalCoordinate(theta, phi);

                Vector4 color = TextureData::SampleTextureCube(cube_map, theta, phi);
                
                // gamma space to linear space
                color.x = std::pow(color.x, 2.2f);
                color.y = std::pow(color.y, 2.2f);
                color.z = std::pow(color.z, 2.2f);
                color.w = std::pow(color.w, 2.2f);

                for (uint32 co_index = 0; co_index < SH2Coefficients::CoefficientsCount; co_index++)
                {
                    float basis = SHBasisFunction(co_index, dir);
                    c.Data[co_index] += color[channel_index] * basis;
                }

            }

            // uniform distribution on unit sphere means there is a same probablity density for every solid angle,
            // which is PDF(w) = 1 / total solid angle = 1 / (4 * PI)
            // since it's a constant, we can apply 1 / PDF(w) to the integral result to get the final value
            for (uint32 co_index = 0; co_index < SH2Coefficients::CoefficientsCount; co_index++)
            {
                c.Data[co_index] *= 4 * PI / SampleCount;
            }

            // calculate irradiance SH coefficients * basis function coefficients
            // so we can get the diffuse term by only multiplying the roughness, coefficients and xyz part of SH basis function
            // see ref for more details
            // ref: https://zhuanlan.zhihu.com/p/144910975 Eq.4
            // ref: https://cseweb.ucsd.edu/~ravir/papers/invlamb/josa.pdf Eq.24
            for (uint32 l = 0; l <= SH2Coefficients::SHOrder; l++)
            {
                for (uint32 m = -l; m <= l; m++)
                {
                    uint32 n = l * l + m + l;

                    float K = sqrt(4 * PI / (2 * l + 1));
                    float L = c.Data[n];
                    float A = CosineSHCoefficients(l);
                    c.Data[n] = InvPI * K * A * L;
                }
            }
        }
    }

    std::array<TextureData, 6> SHBaker::GenerateIrradianceMap(const std::array<TextureData, 6>& cube_map, uint32 map_size, bool debug)
    {
        const ETextureFormat format = ETextureFormat_R8G8B8A8_UNORM;
        std::array<TextureData, 6> irradiance_map =
        {
            TextureData(map_size, map_size, format),
            TextureData(map_size, map_size, format),
            TextureData(map_size, map_size, format),
            TextureData(map_size, map_size, format),
            TextureData(map_size, map_size, format),
            TextureData(map_size, map_size, format),
        };

        SH2Coefficients shr, shg, shb;
        ProjectEnvironmentMap(cube_map, shr, shg, shb);

        SH2CoefficientsPack pack = PackCubeMapSHCoefficient(shr, shg, shb);

        for (uint32 i = 0; i < 6; i++) 
        {
            for (uint32 x = 0; x < map_size; x++)
            {
                for (uint32 y = 0; y < map_size; y++)
                {
                    float u = static_cast<float>(x) / static_cast<float>(map_size);
                    float v = static_cast<float>(y) / static_cast<float>(map_size);

                    Vector3 dir = CalcCubeMapDirection(i, u, v);
                    Vector3 irradiance;
                    if (debug)
                    {
                        irradiance = CalcIrradiance2(shr, shg, shb, dir);
                    }
                    else 
                    {
                        irradiance = CalcIrradiance(pack, dir);
                    }

                    irradiance_map[i].SetPixel(x, y, Vector4(irradiance, 1));
                }
            }
        }
        return irradiance_map;
    }

    // ref: https://zhuanlan.zhihu.com/p/144910975 Eq.4
    SH2CoefficientsPack SHBaker::PackCubeMapSHCoefficient(SH2Coefficients r, SH2Coefficients g, SH2Coefficients b)
    {
        // multiply the SH basis function coefficients to the SH coefficients in advance for runtime efficiency
        for (uint32 i = 0; i < SH2Coefficients::CoefficientsCount; i++) 
        {
            r.Data[i] *= SHBasisFunctionCoefficient(i);
            g.Data[i] *= SHBasisFunctionCoefficient(i);
            b.Data[i] *= SHBasisFunctionCoefficient(i);
        }

        // merge similar terms
        SH2CoefficientsPack ret;
        ret.sha_r = { r.Data[3], r.Data[1], r.Data[2], r.Data[0]};
        ret.shb_r = { r.Data[4], r.Data[5], r.Data[6] * 3, r.Data[7] };
        ret.sha_g = { g.Data[3], g.Data[1], g.Data[2], g.Data[0]};
        ret.shb_g = { g.Data[4], g.Data[5], g.Data[6] * 3, g.Data[7] };
        ret.sha_b = { b.Data[3], b.Data[1], b.Data[2], b.Data[0]};
        ret.shb_b = { b.Data[4], b.Data[5], b.Data[6] * 3, b.Data[7] };
        ret.shc   = { r.Data[8], g.Data[8], b.Data[8] , 0};
        
        return ret;
    }

    Vector3 SHBaker::CalcIrradiance(const SH2CoefficientsPack& pack, const Vector3& normal)
    {
        Vector3 L0L1;
        L0L1.x = pack.sha_r.x * normal.x + pack.sha_r.y * normal.y + pack.sha_r.z * normal.z + pack.sha_r.w;
        L0L1.y = pack.sha_g.x * normal.x + pack.sha_g.y * normal.y + pack.sha_g.z * normal.z + pack.sha_g.w;
        L0L1.z = pack.sha_b.x * normal.x + pack.sha_b.y * normal.y + pack.sha_b.z * normal.z + pack.sha_b.w;

        Vector3 L2;
        float t = normal.x * normal.x - normal.y * normal.y;
        L2.x = pack.shc.x * t;
        L2.y = pack.shc.y * t;
        L2.z = pack.shc.z * t;

        // Monte Carlo integration is not accurate, we need to clamp the result to avoid overflow
        return Vector3::Clamp(L0L1 + L2, Vector3(), Vector3(1, 1, 1));
    }

    Vector3 SHBaker::CalcIrradiance2(const SH2Coefficients& shr, const SH2Coefficients& shg, const SH2Coefficients& shb, const Vector3& normal)
    {
        Vector3 ret;
        for (uint32 i = 0; i < 9; i++)
        {
            float basis = SHBasisFunction(i, normal);
            ret.x += (shr.Data[i] * basis);
            ret.y += (shg.Data[i] * basis);
            ret.z += (shb.Data[i] * basis);
        }

        // Monte Carlo integration is not accurate, we need to clamp the result to avoid overflow
        return Vector3::Clamp(ret, Vector3(), Vector3(1, 1, 1));
    }
}