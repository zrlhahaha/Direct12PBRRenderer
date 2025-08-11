#include "global.hlsli"

#define THREAD_GROUP_SIZE 16

Texture2D<float4> InputTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);

cbuffer CONSTANT_BUFFER_SHADER : register(b0)
{
    float2 TexelSize;
    float Threshold;
    float Knee;
}

// https://catlikecoding.com/unity/tutorials/custom-srp/post-processing/ section 2.7
// for selecting bright pixels
float4 bloom_threshold(float4 color, float threshold, float knee)
{
    float brightness = max(color.x, max(color.y, color.z));

    float soft = min(max(brightness - threshold + threshold * knee, 0.0), 2 * threshold * knee);
    soft /= 4 * threshold * knee + 0.00001;

    float contribution  = max(soft, brightness - threshold) / max(brightness, 0.00001);
    return float4(color.xyz * contribution, 1.0);
}

// https://catlikecoding.com/unity/tutorials/custom-srp/hdr/ section 1.5
// for suppressing fireflies
float4 cross_filter(Texture2D<float4> input, float2 uv, float2 texel_size)
{
    const int NUM_OFFSETS = 5;
    float2 offsets[NUM_OFFSETS] = {float2(0.0, 0.0), float2(-1.0, -1.0), float2(-1.0, 1.0), float2(1.0, -1.0), float2(1.0, 1.0)};

    float3 total_color = float3(0.0, 0.0, 0.0);
    float total_weight = 0.0;

    for(int i = 0; i < NUM_OFFSETS; i++)
    {
        float4 color = input.SampleLevel(SamplerLinearClamp, uv + offsets[i] * texel_size, 0);
        color = bloom_threshold(color, Threshold, Knee);
        float weight = 1.0 / (luminance(color.xyz) + 1.0);

        total_color += color.xyz * weight;
        total_weight += weight;
    }

    if(total_weight > 0.0)
    {
        total_color /= total_weight;
    }

    return float4(total_color, 1.0);
}

[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, 1)]
void cs_main(int3 dtid : SV_DispatchThreadID, int3 gtid : SV_GroupThreadID)
{
    OutputTexture[dtid.xy] = cross_filter(InputTexture, dtid.xy * TexelSize, TexelSize);
}