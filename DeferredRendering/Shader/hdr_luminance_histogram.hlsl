# include "global.hlsli"

#define NUM_HISTOGRAM_BINS 256
#define HISTOGRAM_THREAD_GROUP_SIZE 16

// compute histogram of @LuminanceTexture
// ref https://www.alextardif.com/HistogramLuminance.html

Texture2D LuminanceTexture : register(t0);
RWStructuredBuffer<uint> LuminanceHistogram : register(u0);

cbuffer CONSTANT_BUFFER_SHADER : register(b0)
{
    uint TextureWidth;
    uint TextureHeight;
    float MinLogLuminance;
    float InvLogLuminanceRange; 
};

groupshared uint HistogramShared[NUM_HISTOGRAM_BINS];

// put different luminance values into different histogram bins
uint LuminanceToHistogramBin(float luminance)
{
    if (luminance < EPSILON)
    {
        return 0;
    }

    // basically it log2(luminance) first, then map it to [0, 1] range
    float log_luminance = saturate((log2(luminance) - MinLogLuminance) * InvLogLuminanceRange);

    // [0, 1] range to [1 - 255] range, 0th bin is ignored, so the super dark area won't contribute to the average luminance
    return floor(log_luminance  * (NUM_HISTOGRAM_BINS - 2) + 1.0);
}

[numthreads(HISTOGRAM_THREAD_GROUP_SIZE, HISTOGRAM_THREAD_GROUP_SIZE, 1)]
void cs_main(uint3 dtid : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID)
{
    // clear the group shared memory, assume HISTOGRAM_THREAD_GROUP_SIZE * HISTOGRAM_THREAD_GROUP_SIZE == NUM_HISTOGRAM_BINS
    uint bin_index = gtid.x + gtid.y * HISTOGRAM_THREAD_GROUP_SIZE;
    HistogramShared[bin_index] = 0;

    GroupMemoryBarrierWithGroupSync();

    // for each @LuminanceTexture pixel, interloackedadd 1 to the corresponding group shared memory
    if (dtid.x < TextureWidth && dtid.y < TextureHeight)
    {
        float3 hdr_color = LuminanceTexture[dtid.xy].rgb;
        uint index = LuminanceToHistogramBin(luminance(hdr_color));
        InterlockedAdd(HistogramShared[index], 1);
    }

    // wait all threads in this thread group to finish
    GroupMemoryBarrierWithGroupSync();

    // add up all the @HistogramShared from every thread group to the structured buffer
    InterlockedAdd(LuminanceHistogram[bin_index], HistogramShared[bin_index]);
}