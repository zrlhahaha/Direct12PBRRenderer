#include "global.hlsli"
#define THREAD_GROUP_SIZE 16

Texture2D<float4> InputTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);

[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, 1)]
void cs_main(int3 dtid : SV_DispatchThreadID, int3 gtid : SV_GroupThreadID)
{
    OutputTexture[dtid.xy] = OutputTexture[dtid.xy] + InputTexture[dtid.xy];
}