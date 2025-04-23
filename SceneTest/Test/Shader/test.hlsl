Texture2D BaseColorMap : register(t0);
SamplerState sampleWrap : register(s0);

cbuffer GlobalConstant : register(b0)
{
}


cbuffer ShaderConstant : register(b1)
{
}

cbuffer InstanceConstant : register(b2)
{
    float4x4 model;
    float4x4 view;
    float4x4 projection;
}


struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float3 tangent : COLOR;
    float2 uv : TEXCOORD0;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 worldpos : POSITION;
    float2 uv : TEXCOORD0;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
};


PSInput vs_main(VSInput vertex)
{
    PSInput result;

    result.worldpos = mul(model, float4(vertex.position, 1));
    result.position = mul(projection, mul(view, result.worldpos));
    result.uv = vertex.uv;
    result.normal = vertex.normal;
    result.tangent = vertex.tangent;

    return result;
}

float4 ps_main(PSInput input) : SV_TARGET
{
    float4 base_color = BaseColorMap.Sample(sampleWrap, input.uv);
    float3 normal_world = saturate(mul(model, float4(input.normal, 0))).xyz;
    float NoL = saturate(dot(normalize(normal_world), normalize(float3(1, 1, 1))));

    return base_color * NoL;
}
