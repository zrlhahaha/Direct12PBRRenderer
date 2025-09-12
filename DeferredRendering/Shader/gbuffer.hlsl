#include"global.hlsli"

Texture2D AlbedoMap : register(t0);
Texture2D NormalMap : register(t1);
Texture2D RoughnessMap : register(t2);
Texture2D MetallicMap : register(t3);
Texture2D AmbientOcclusionMap : register(t4);


// GBufferA R8G8B8A8
//  |---8-bits---||---8-bits---||---8-bits---||----8-bits----|
//  |---------------base color---------------||---emission---|


// GBufferB: R16G16
//  |---------16-bits--- ------||---------16-bits--- ------|
//  |---------normal.x---------||---------normal.y---------|

// GBufferC: |---8-bits---||---8-bits---||---8-bits---||---8-bits---|
//           |--roughness-||--metalic---||-----AO-----||---unused---|
struct GBuffer
{
    float4 GBufferA: SV_Target0;
    float4 GBufferB: SV_Target1;
    float4 GBufferC: SV_Target2;
};

// warn: make sure constant buffer memory layouts in hlsl and c++ are the same, or the parameters will be assigned randomly
cbuffer CONSTANT_BUFFER_SHADER : register(CONSTANT_BUFFER_REGISTER_SHADER)
{
}

cbuffer CONSTANT_BUFFER_INSTANCE : register(CONSTANT_BUFFER_REGISTER_INSTANCE)
{
    float4x4 Model;
    float4x4 InvModel;

    float3 Albedo;
    float Emission;
    float Roughness;
    float Metallic;

    bool UseAlbedoMap;
    bool UseNormalMap;
    bool UseMetallicMap;
    bool UseRoughnessMap;
    bool UseAmbientOcclusionMap;
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
float3 sample_normal_texture(float2 uv, float3 normal, float3 tangent)
{
    float3 bitangent = cross(normal, tangent);
    float3x3 TBN = float3x3(tangent, bitangent, normal);

    float3 normal_ts = NormalMap.Sample(SamplerLinearWrap, uv).rgb * 2 - 1;
    return normalize(mul(normal_ts, TBN));
}

PSInput vs_main(VSInput_P3F_N3F_T2F_T2F vertex)
{
    PSInput output;

    // ref: UnityShader入门精要 section 4.7
    // we use the transpose of the inverse model matrix to transform the normal
    output.position_ws = mul(Model, float4(vertex.position, 1));
    output.normal_ws = mul(transpose(InvModel), float4(vertex.normal, 0)).xyz;
    output.tangent_ws = mul(transpose(InvModel), float4(vertex.tangent, 0)).xyz;

    float4 position_vs = mul(View, output.position_ws);
    output.position = mul(Projection, position_vs);
    output.uv = vertex.uv;

    return output;
}

GBuffer ps_main(PSInput input)
{
    GBuffer output;

    // collect principaled brdf parameters
    float3 albedo;
    float3 normal_ws;
    float roughness;
    float metallic;
    float ambient_occlusion;

    if(UseNormalMap)
    {
        normal_ws = sample_normal_texture(input.uv, normalize(input.normal_ws), normalize(input.tangent_ws));
    }
    else
    {
        normal_ws = normalize(input.normal_ws);
    }

    if(UseAlbedoMap)
    {
        albedo = decode_gamma(AlbedoMap.Sample(SamplerLinearWrap, input.uv).rgb);
    }
    else
    {
        albedo = decode_gamma(Albedo);
    }

    if(UseRoughnessMap)
    {
        roughness = RoughnessMap.Sample(SamplerLinearWrap, input.uv).x;
    }
    else
    {
        roughness = Roughness;
    }

    if(UseMetallicMap)
    {
        metallic = MetallicMap.Sample(SamplerLinearWrap, input.uv).x;
    }
    else
    {
        metallic = Metallic;
    }

    if(UseAmbientOcclusionMap)
    {
        ambient_occlusion = AmbientOcclusionMap.Sample(SamplerLinearWrap, input.uv).x;
    }
    else
    {
        ambient_occlusion = 0.0;
    }

    output.GBufferA = float4(albedo, Emission);
    output.GBufferB = float4(pack_normal(normal_ws), 1, 0);
    output.GBufferC = float4(roughness, metallic, ambient_occlusion, 0);

    return output;
}
