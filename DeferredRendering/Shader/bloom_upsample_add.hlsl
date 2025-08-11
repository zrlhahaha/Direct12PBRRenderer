#include "global.hlsli"
#include "blur.hlsli"

Texture2D<float4> UpperLevel : register(t0);
Texture2D<float4> LowerLevel : register(t1);
RWTexture2D<float4> OutputTexture : register(u0);

cbuffer CONSTANT_BUFFER_SHADER : register(CONSTANT_BUFFER_REGISTER_SHADER)
{
    float2 TexelSize;
}

[numthreads(BLUR_H_THREAD_GROUP_SIZE_X, BLUR_H_THREAD_GROUP_SIZE_Y, BLUR_H_THREAD_GROUP_SIZE_Z)]
void cs_main(int3 dtid : SV_DispatchThreadID, int3 gtid : SV_GroupThreadID)
{
    // guassian filter lower level and upper level
    float4 lower_value = blur_horizontal(LowerLevel, dtid, gtid, TexelSize);
    
    GroupMemoryBarrierWithGroupSync();
    
    float4 upper_value = blur_horizontal(UpperLevel, dtid, gtid, TexelSize);

    // add the two results
    OutputTexture[dtid.xy] = lower_value + upper_value;
}
