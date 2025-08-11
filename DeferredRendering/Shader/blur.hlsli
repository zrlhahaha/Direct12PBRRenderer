#ifndef H_BLUR
#define H_BLUR

#include "global.hlsli"

#define BLUR_RADIUS 4
#define BLUR_KERNEL_SIZE (BLUR_RADIUS * 2 + 1)

#define BLUR_H_THREAD_GROUP_SIZE_X 256
#define BLUR_H_THREAD_GROUP_SIZE_Y 1
#define BLUR_H_THREAD_GROUP_SIZE_Z 1

#define BLUR_V_THREAD_GROUP_SIZE_X 1
#define BLUR_V_THREAD_GROUP_SIZE_Y 256
#define BLUR_V_THREAD_GROUP_SIZE_Z 1

static const float GAUSS_WEIGHT[BLUR_KERNEL_SIZE] = {0.0148, 0.0459, 0.1050, 0.1941, 0.2803, 0.1941, 0.1050, 0.0459, 0.0148};

// @Cache covers all the pixels that will be sampled during convolution
groupshared float4 Cache[BLUR_H_THREAD_GROUP_SIZE_X + 2 * BLUR_RADIUS];


// horizontal gaussian blur
float4 blur_horizontal(Texture2D<float4> input_tex, int3 dtid, int3 gtid, float2 texel_size)
{
    float2 uv = (dtid.xy + 0.5) * texel_size;

    // step 1: clamp and cache the pixel values into shared memory
    if(gtid.x < BLUR_RADIUS)
    {
        float x = max(uv.x - BLUR_RADIUS * texel_size.x, 0);
        Cache[gtid.x] = input_tex.SampleLevel(SamplerLinearClamp, float2(x, uv.y), 0);
    }

    if(gtid.x >= BLUR_H_THREAD_GROUP_SIZE_X - BLUR_RADIUS)
    {
        float x = min(uv.x + BLUR_RADIUS * texel_size.x, 1.0);
        Cache[gtid.x + 2 * BLUR_RADIUS] = input_tex.SampleLevel(SamplerLinearClamp, float2(x, uv.y), 0);
    }

    Cache[gtid.x + BLUR_RADIUS] = input_tex.SampleLevel(SamplerLinearClamp, uv, 0);

    GroupMemoryBarrierWithGroupSync();

    // step 2: guassian blur
    float4 value = float4(0, 0, 0, 0);
    for(int i = -BLUR_RADIUS; i <= BLUR_RADIUS; i++)
    {
        float4 pixel = Cache[gtid.x + i + BLUR_RADIUS];
        float weight = GAUSS_WEIGHT[i + BLUR_RADIUS];
        value += pixel * weight;
    }

    return value;
}

// vertical gaussian blur, assume @input has same resolution as @output
float4 blur_vertical(Texture2D<float4> input_tex, int3 dtid, int3 gtid, float2 texel_size)
{
    float2 uv = (dtid.xy + 0.5) * texel_size;

    // step 1: clamp and cache the pixel values into shared memory
    if(gtid.y < BLUR_RADIUS)
    {
        float y = max(uv.y - BLUR_RADIUS * texel_size.y, 0);
        Cache[gtid.y] = input_tex.SampleLevel(SamplerLinearClamp, float2(uv.x, y), 0);
    }

    if(gtid.y >= BLUR_V_THREAD_GROUP_SIZE_Y - BLUR_RADIUS)
    {
        float y = min(uv.y + BLUR_RADIUS * texel_size.y, 1.0);
        Cache[gtid.y + 2 * BLUR_RADIUS] = input_tex.SampleLevel(SamplerLinearClamp, float2(uv.x, y), 0);
    }

    Cache[gtid.y + BLUR_RADIUS] = input_tex.SampleLevel(SamplerLinearClamp, uv, 0);

    GroupMemoryBarrierWithGroupSync();

    // step 2: guassian blur
    float4 value = float4(0, 0, 0, 0);
    for(int i = -BLUR_RADIUS; i <= BLUR_RADIUS; i++)
    {
        float4 pixel = Cache[gtid.y + i + BLUR_RADIUS];
        float weight = GAUSS_WEIGHT[i + BLUR_RADIUS];
        value += pixel * weight;
    }

    return value;
}
#endif