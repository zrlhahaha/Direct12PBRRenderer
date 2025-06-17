#include "global.hlsli"
#include "brdf.hlsli"
#define SAMPLE_COUNT 1024

cbuffer ShaderConstant
{
    uint TextureResolution;
}

RWTexture2D<float2> PrecomputeBRDF;

// This compute shader will generate the lut for the hemishpere integral of the brdf f(l, v)
// It only depends on NdotV and roughness since it the integral about the @l on the hemisphere,
// and the function is bidirectional which means it only depends on the angle between @v and @n.

// ref: [1] https://learnopengl.com/PBR/IBL/Specular-IBL
//      [2] https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf section. Environment BRDF
//      [3] https://seblagarde.wordpress.com/wp-content/uploads/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf eq.58
//      [3] https://zhuanlan.zhihu.com/p/162793239
[numthreads(8, 8, 1)]
void cs_main(uint3 dtid : SV_DispatchThreadID)
{
    float roughness = (float)dtid.x / (float)(TextureResolution - 1); // [0, 1]
    float NdotV =(float)(dtid.y + 1) / (float)(TextureResolution); // +1 makes [0, 1] => (0, 1], to avoid black pixel at grazing angle, NdotV = 0 will cause brdf equal to 0

    // since the whole lut is only depends on NdotV and roughness
    // we assume normal = (0, 0, 1), so V = (sin(theta), 0, cos(theta)) = (sqrt(1 - NdotV * NdotV), 0, NdotV)
    float3 V = float3(sqrt(1 - NdotV * NdotV), 0, NdotV);
    float3 N = float3(0, 0, 1);

    float A = 0.0;
    float B = 0.0;
    for(uint i = 0; i < SAMPLE_COUNT; i++)
    {
        // ggx importance sampling
        float2 xi = hammersley(i, SAMPLE_COUNT);
        float3 H = ggx_important_sample(roughness, N, xi);
        float3 L = normalize(2 * dot(V, H) * H - V);

        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V, H), 0);

        if(NdotL > 0.0)
        {
            float Fc = pow(1.0 - VdotH, 5.0);

            float k = roughness * roughness / 2.0;
            float G = geometry_smith(NdotL, NdotV, k);

            float G_Vis = (G * VdotH) / max((NdotH * NdotV), 0.0001);

            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }

    A = A / (float)SAMPLE_COUNT;
    B = B / (float)SAMPLE_COUNT;

    PrecomputeBRDF[dtid.xy] = float2(A, B);
}