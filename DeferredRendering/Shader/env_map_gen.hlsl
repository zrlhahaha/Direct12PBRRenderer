#include "global.hlsli" 
#include "brdf.hlsli"
#define SAMPLE_COUNT 1024
#define THREAD_GROUP_SIZE 8

TextureCube SkyBox;
RWTexture2DArray<float4> PrefilterEnvMap[PREFILTER_ENVMAP_MIPMAP_SIZE];

cbuffer CONSTANT_BUFFER_SHADER
{
    float Roughness;
    uint MipLevel;
    uint PrefilterEnvMapTextureSize;
}

// cubemap coordinates defination: https://learn.microsoft.com/en-us/windows/win32/direct3d9/cubic-environment-mapping
// slice_index: the @PrefilterEnvMap contains 6 slices representing the 6 faces of a cubemap.
//              the slice_index indexes them in the order: +X, -X, +Y, -Y, +Z, -Z.
// u, v: the uv coordinates of the cubemap face, in the range [0, 1].
float3 calc_cubemap_dir(uint slice_index, float u, float v)
{
    // map uv from [0, 1] to [-1, 1]
    u = 2 * u - 1;
    v = 2 * v - 1;

    // +x, -x, +y, -y, +z, -z
    switch (slice_index)
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
//      https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf section. Image-Based Lighting
//      https://seblagarde.wordpress.com/wp-content/uploads/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf eq.53
[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, PREFILTER_ENVMAP_MIPMAP_SIZE)]
 void cs_main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint texture_size = (PrefilterEnvMapTextureSize >> MipLevel);
    float2 xy = float2(dispatch_thread_id.xy) / texture_size;
    uint slice_index = dispatch_thread_id.z;

    // assume V = N = R, and use R to sample PrefilterEnvMap when comes to runtime.
    // V: direction to the camera
    // N: normal of the surface
    // R: reflection direction of V
    float3 R = calc_cubemap_dir(slice_index, xy.x, xy.y);
    float3 N = R;
    float3 V = R;

    // monte-carlo integration and ggx importance sampling
    float3 total_color = float3(0, 0, 0);
    float total_weight = 0;

    for(uint i = 0; i < SAMPLE_COUNT; i++)
    {
        float2 xi = hammersley(i, SAMPLE_COUNT);

        // generate microfacet normal
        float3 H = ggx_important_sample(Roughness, N, xi);
        float3 L = normalize(2 * dot(V, H) * H - V);
        float NdotL = max(dot(N, L), 0);

        // trillinear sampling cubemap mipmap chain based on PDF of the incident direction
        // ref: https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-20-gpu-based-importance-sampling section. 20.4
        //      https://chetanjags.wordpress.com/2015/08/26/image-based-lighting/
        if(NdotL > 0)
        {
            float NdotH = max(dot(N, H), 0);
            float HdotV = max(dot(H, V), 0);

            float D = distribution_ggx(NdotH, Roughness);
            float pdf = D * NdotH / (4.0 * HdotV + 0.0001); // pdf of the incident direction

            float texel_sa = 4.0 * PI / (NUM_CUBEMAP_FACES * PrefilterEnvMapTextureSize * PrefilterEnvMapTextureSize); // per pixel solid angle
            float sample_sa = 1.0 / (float(SAMPLE_COUNT) * pdf + 0.0001); // solid angle for the incident direction

            float mip_level = Roughness == 0.0 ? 0.0 : 0.5 * log2(sample_sa / texel_sa);

            // assume SkyBox is a HDRI cubemap and it's in linear space, so we don't need gamma correction here
            float3 color = SkyBox.SampleLevel(SamplerLinearClamp, L, mip_level).rgb;

            // follow UE paper, we use NdotL weighted average rather than ordinary monte-carlo
            total_color += color * NdotL;
            total_weight += NdotL;
        }
    }
    total_color /= total_weight;

    PrefilterEnvMap[MipLevel][uint3(dispatch_thread_id.xy, slice_index)] = float4(total_color, 1);
}