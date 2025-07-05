#include "global.hlsli"
#include "brdf.hlsli"

Texture2D GBufferA;
Texture2D GBufferB;
Texture2D GBufferC;
Texture2D DepthStencil;
TextureCube PrefilterEnvMap;
Texture2D PrecomputeBRDF;

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float3 camera_vec: COLOR0;
};

//calculate diffuse term in pbr by solving SH polynomial
float3 EnvironmentDiffuse(float3 base_color, float metallic, float3 normal)
{
    float4 a = float4(normal, 1);
    float4 b = normal.xyzz * normal.yzzx;
    float c = normal.x * normal.x - normal.y * normal.y;
    
    // band 0 and band 1
    float3 L0L1;
    L0L1.x = dot(SkyBoxSH.SHA_R, a);
    L0L1.y = dot(SkyBoxSH.SHA_G, a);
    L0L1.z = dot(SkyBoxSH.SHA_B, a);

    // band 2
    float3 L2;
    L2.x = dot(SkyBoxSH.SHB_R, b);
    L2.y = dot(SkyBoxSH.SHB_G, b);
    L2.z = dot(SkyBoxSH.SHB_B, b);
    L2 += SkyBoxSH.SHB_C.xyz * c;

    float3 irradiance = (L0L1 + L2);
    
    // ref https://zhuanlan.zhihu.com/p/144910975 eq.4
    // //  https://learnopengl.com/PBR/IBL/Diffuse-irradiance
    // float3 F0 = ComputeF0(input);
    // float3 F = FresnelSchlick(max(dot(input.NormalWS, eyeWS), 0.0), F0, input.Roughness);;
    // float3 kS = F;
    // float3 kD = 1.0 - kS;
    // kD *= 1.0 - input.Metallic;

    float3 kd = base_color * (1 - metallic) * INV_PI;
    return kd * irradiance;
}

float3 EnvironmentSpecular(float3 N, float3 V, float3 F0, float roughness)
{
    float NdotV = max(dot(N, V), 0);
    float3 R = normalize(2 * dot(N, V) * N - V);
    float3 env_irradiance = PrefilterEnvMap.SampleLevel(SamplerLinearClamp, R, roughness * PREFILTER_ENVMAP_MIPMAP_SIZE).rgb;
    float2 env_brdf = PrecomputeBRDF.Sample(SamplerLinearClamp, float2(roughness, NdotV)).rg;

    // evaluate the split-sum equation
    // ref: https://zhuanlan.zhihu.com/p/162793239 
    // ref: https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf eq.8

    // somehow learnopengl.com use schlick-fresnel rather than F0, 
    // ref: https://learnopengl.com/PBR/IBL/Specular-IBL
    return env_irradiance * (F0 * env_brdf.x + env_brdf.y);
}

// for ProjectionMatrix1::ProjectionMatrix1 only, which ndc.z -> [0, 1]
// return depth value in view space, which belongs to [Near, Far]
float ViewSpaceDepth(float ndc_depth)
{
    return Near * Far / (Far - ndc_depth * (Far - Near));
}

float3 ViewSpacePosition(float3 camera_vec, float ndc_depth)
{
    float depth = ViewSpaceDepth(ndc_depth);
    return CameraPos + camera_vec * depth / Near;
}

PSInput vs_main(VSInput_P3F_T2F vertex)
{
    PSInput output;
    float near_height = 2 * Near * tan(Fov / 2);
    float near_width = near_height * Ratio;

    // camera_vec means vector from camera to point on near plane
    // see the initialization code of mScreenVertexBuffer in D3D12Device.cpp for more infomation
    float3 camera_vec;
    if(vertex.position.y > 0)
    {
        camera_vec = float3(-0.5, 1.5, 1);
    }
    else
    {
        if(vertex.position.x < 0)
        {
            camera_vec = float3(-0.5, -0.5, 1);
        }
        else
        {
            camera_vec = float3(1.5, -0.5, 1);
        }
    }

    camera_vec *= float3(near_width, near_height, Near);
    output.camera_vec = mul(InvView, float4(camera_vec, 0)).xyz;
    output.uv = vertex.uv;
    output.position = float4(vertex.position, 1);
    return output;
}

float4 ps_main(PSInput input) : SV_TARGET
{
    float3 albedo = GBufferA.Sample(SamplerPointClamp, input.uv).rgb;
    float3 mixed = GBufferC.Sample(SamplerPointClamp, input.uv).rgb;
    float3 normal_ws = unpack_normal(GBufferB.Sample(SamplerPointClamp, input.uv).rg);

    float depth_ndc = DepthStencil.Sample(SamplerLinearClamp, input.uv).r;
    float3 position_ws = ViewSpacePosition(input.camera_vec, depth_ndc);
    float3 view_ws = normalize(CameraPos - position_ws);
    
    float roughness = mixed.x;
    float metallic = mixed.y;
    float ao = mixed.z;

    // indirect light
    float3 env_diffuse = EnvironmentDiffuse(albedo, metallic, normal_ws);
    float3 env_specular = EnvironmentSpecular(normal_ws, normalize(CameraPos - position_ws), compute_F0(albedo, metallic), roughness);

    float3 light_dir_ws = normalize(float3(1, 1, 1));
    float3 light_color = float3(1, 1, 1);
    float3 light_luminance = float3(100, 100, 100);

    // direct light
    BRDFInput brdf_input;
    brdf_input.Metallic = metallic;
    brdf_input.Roughness = roughness;
    brdf_input.Albedo = albedo;
    brdf_input.Normal = normal_ws;
    brdf_input.ViewDir = view_ws;
    brdf_input.LightDir = light_dir_ws;

    float3 direct_luminance = brdf(brdf_input) * light_color * light_luminance * max(dot(normal_ws, light_dir_ws), 0.0);

    return float4(env_diffuse + env_specular, 1);
    // return float4(max(dot(normal_ws.y, float3(0,1,0)), 0).xxx, 1);
}