#include "global.hlsli"

#define NUM_HISTOGRAM_BINS 256
#define SMOOTH_TIME 1.6

// compute the average luminance of @LuminanceTexture
// ref https://www.alextardif.com/HistogramLuminance.html
//     https://bruop.github.io/exposure/

// equation for compute average luminance:
// https://expf.wordpress.com/2010/05/04/reinhards_tone_mapping_operator/

RWStructuredBuffer<uint> LuminanceHistogram : register(u0); // size = NUM_HISTOGRAM_BINS
RWStructuredBuffer<float> AverageLuminance : register(u1); // size = 1

cbuffer CONSTANT_BUFFER_SHADER : register(b0)
{
    uint PixelCount;
    float MinLogLuminance;
    float LogLuminanceRange;
};

groupshared float HistogramShared[NUM_HISTOGRAM_BINS];

// the inverse function of @LuminanceToHistogramBin
float BinIndexToLuminance(uint bin_index)
{
    // [1, 255]  to [0.0, 1.0]
    float log_luminance = (bin_index - 1.0) / (NUM_HISTOGRAM_BINS - 2);

    // [0.0, 1.0] to luminance range
    return exp2(log_luminance * LogLuminanceRange + MinLogLuminance);
}

[numthreads(NUM_HISTOGRAM_BINS, 1, 1)]
void cs_main(uint3 gtid : SV_GroupThreadID)
{
    uint index = gtid.x;
    uint num_pixels = LuminanceHistogram[index];
    
    // find the average bin in the histogram
    HistogramShared[index] = num_pixels * index; // [0, 255 * num_pixels]

    GroupMemoryBarrierWithGroupSync();

    LuminanceHistogram[index] = 0; // clear the histogram

    // parallel reduction
    for (uint step = (NUM_HISTOGRAM_BINS >> 1); step > 0; step >>= 1)
    {
        if (index < step)
        {
            HistogramShared[index] += HistogramShared[index + step];
        }
        
        GroupMemoryBarrierWithGroupSync();
    }

    if (index == 0)
    {
        // divide sum by pixels count to get the average bin.
        // ignore the first bin, which means we ignore the almost black area in the image
        float sum_value = HistogramShared[0];
        float average_bin = sum_value / (float)(PixelCount - num_pixels); // [0, 255]
        float luminance = BinIndexToLuminance(average_bin); // [0, 1]

        // lerp the average luminance with the last frame luminance to get the final result.
        // exp(DelteTime) prevents the average luminance from changing too fast if the DeltaTime is to large, e.g screen is frozen for a while
        // https://www.desmos.com/calculator/fguxmvdcqr
        float prev_luminance = AverageLuminance[0];
        AverageLuminance[0] = lerp(prev_luminance, luminance, saturate((1 - exp(-DeltaTime * SMOOTH_TIME)))); 
    }
}