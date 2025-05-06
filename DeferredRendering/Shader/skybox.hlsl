#include "global.hlsli"

TextureCube SkyBox;

struct PSInput
{
    float4 position : SV_POSITION;
    float3 position_ls : POSITION;
    float2 uv : TEXCOORD0; 
};

PSInput vs_main(VSInput_P3F_T2F vertex)
{
    PSInput output;

    float3 pos_vs = mul(View, float4(vertex.position * Far + CameraPos, 1)).xyz;
    output.position = mul(Projection, float4(pos_vs, 1));
    output.position_ls = normalize(vertex.position);
    output.uv = vertex.uv;
    return output;
}

float4 ps_main(PSInput input) : SV_TARGET
{
    float3 color = SkyBox.Sample(SamplerLinearWrap, input.position_ls).rgb;
    return float4(color, 1);
}