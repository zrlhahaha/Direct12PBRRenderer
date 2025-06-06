# ifndef H_BLIT
# define H_BLIT

#include "global.hlsli"

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0; 
};

PSInput vs_main(VSInput_P3F_T2F vertex)
{
    PSInput output;

    output.position = float4(vertex.position, 1);
    output.uv = vertex.uv;
    return output;
}
#endif