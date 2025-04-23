#include "global.hlsli" 
#define SampleCount 1024

TextureCube SkyBox;
RWTexture2DArray<float4> PrefilterEnvMap[PrefilterEnvMapMipSize];

cbuffer ShaderConstant
{
    float Roughness;
    uint MipLevel;
    uint PrefilterEnvMapSize;
}

float3 calc_cubemap_dir(uint mip_index, float u, float v)
{
    // map uv from [0, 1] to [-1, 1]
    u = 2 * u - 1;
    v = 2 * v - 1;

    // +x, -x, +y, -y, +z, -z
    switch (mip_index)
    {
        case 0:
            return normalize(float3(1, -v, -u));
        case 1:
            return normalize(float3(-1, -v, u));
        case 2:
            return normalize(float3(u, 1, v));
        case 3:
            return normalize(float3(u, -1, -v));
        case 4:
            return normalize(float3(u, -v, 1));
        case 5:
            return normalize(float3(-u, -v, -1));
    }

    return float3(0, 0, 1);
}

// generate prefilter env map of a cubemap
// ref: https://zhuanlan.zhihu.com/p/162793239#ref_3_0
[numthreads(8, 8, 6)]
 void cs_main(uint3 dispatch_thread_id : SV_DispatchThreadID)
 {
    uint mipmap_size = (PrefilterEnvMapSize >> MipLevel);
    float2 xy = float2(dispatch_thread_id.xy) / mipmap_size;
    uint mip_index = dispatch_thread_id.z;

    // assume V = N = R
    // V: direction to the camera
    // N: normal of the surface
    // R: reflection of V
    
    // we will use the V and N to do the importance sampling
    // and use R to sample PrefilterEnvMap when comes to runtime.
    float3 R = calc_cubemap_dir(mip_index, xy.x, xy.y);
    float3 N = R;
    float3 V = R;

    // monte-carlo integration and ggx importance sampling
    float3 total_color = float3(0, 0, 0);

    for(uint i = 0; i < SampleCount; i++)
    {
        float2 xi = hammersley(i, SampleCount);

        // generate microfacet normal
        float3 H = ggx_important_sample(Roughness, N, xi);

        float3 L = normalize(2 * dot(V, H) * H - V);
        float3 color = decode_gamma(SkyBox.SampleLevel(SamplerLinearClamp, L, 0).rgb);
        float NoL = max(0, dot(H, L));

        total_color += color * NoL;
    }
    total_color /= SampleCount;

    PrefilterEnvMap[MipLevel][uint3(dispatch_thread_id.xy, mip_index)] = float4(total_color, 1);
 }