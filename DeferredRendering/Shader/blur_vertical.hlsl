#include "blur.hlsli"

Texture2D<float4> InputTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);

cbuffer CONSTANT_BUFFER_SHADER : register(CONSTANT_BUFFER_REGISTER_SHADER)
{
    float2 TexelSize;
}

[numthreads(BLUR_V_THREAD_GROUP_SIZE_X, BLUR_V_THREAD_GROUP_SIZE_Y, BLUR_V_THREAD_GROUP_SIZE_Z)]
void cs_main(int3 dtid : SV_DispatchThreadID, int3 gtid : SV_GroupThreadID)
{
    OutputTexture[dtid.xy] = blur_vertical(InputTexture, dtid, gtid, TexelSize);
}
