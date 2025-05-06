#include"global.hlsli"

Texture2D BaseColorMap : register(t0);
Texture2D NormalMap : register(t1);
Texture2D MixedMap : register(t2);

cbuffer ShaderConstant : register(ShaderConstantBufferRegister)
{
}

cbuffer InstanceConstant : register(InstanceConstantBufferRegister)
{
    float4x4 Model;
    float4x4 InvModel;
}

struct PSInput
{
    float4 position : SV_POSITION;
    float4 position_ws : POSITION;
    float3 normal_ws : NORMAL0;
    float3 tangent_ws : TANGENT0;
    float3 color : COLOR0;
    float2 uv : TEXCOORD0;
};

// sample and transform normal from tangent space to object space
// normal and tangent need to be unit vectiors
float3 sample_normal(float2 uv, float3 normal, float3 tangent)
{
    float3 bitangent = cross(normal, tangent);
    float3x3 TBN = float3x3(tangent, bitangent, normal);
    
    float3 color = NormalMap.Sample(SamplerLinearWrap, uv).rgb;
    if(color.x == 0 && color.y == 0 && color.z == 0)
    {
        // not normal map is provided
        return normal;
    }
    else
    {
        float3 normal_ts = color * 2 - 1;
        float3 normal_os = mul(TBN, normal_ts);
        return normalize(normal_os);
    }
}

PSInput vs_main(VSInput_P3F_N3F_T2F_T2F vertex)
{
    PSInput output;

    output.position_ws = mul(Model, float4(vertex.position, 1));
    output.normal_ws = mul(Model, float4(vertex.normal, 0)).xyz;
    output.tangent_ws = mul(Model, float4(vertex.tangent, 0)).xyz;

    float4 position_vs = mul(View, output.position_ws);
    output.position = mul(Projection, position_vs);
    output.uv = vertex.uv;

    return output;
}

GBuffer ps_main(PSInput input)
{
    GBuffer output;
    float3 normal_os = sample_normal(input.uv, normalize(input.normal_ws), normalize(input.tangent_ws));

    // ref: UnityShader入门精要 4.7
    // we use the transpose of the inverse model matrix to transform the normal
    float3 normal_ws = mul(transpose(InvModel), float4(normal_os, 0)).xyz;

    output.GBufferA = float4(decode_gamma(BaseColorMap.Sample(SamplerLinearWrap, input.uv).rgb), 0);
    // output.GBufferA = float4(float3(1,1,1), 0);
    output.GBufferB = float4(pack_normal(normalize(input.normal_ws)), 1, 0);

    // every asset comes from https://www.fab.com/listings/4da78da6-44b3-4adf-8883-219fe17b44d4
    // accroding to the shadergraph, the mixed map is composed of [ambient occlusion, roughness, metallic, 0]
    float4 mixed = MixedMap.Sample(SamplerLinearWrap, input.uv);
    output.GBufferC = float4(mixed.g, mixed.b, mixed.a, 0);
    return output;
}
