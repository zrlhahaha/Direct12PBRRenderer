#ifndef H_BRDF
#define H_BRDF

// defination of D F G term in brdf equation
// ref: https://learnopengl.com/PBR/Theory
float gtr2(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float t = (NdotH * NdotH) * (a * a - 1) + 1;
    return a * a / (PI * t * t);
}

float3 fresnel(float NdotL, float3 F0)
{
    return F0 + (1 - F0) * pow(1 - NdotL, 5);
}

float geometry_schlick_ggx(float NdotV, float k)
{
    return NdotV / (NdotV * (1 - k) + k);
}

float geometry_smith(float NdotL, float NdotV, float k)
{
    float ggx1 = geometry_schlick_ggx(NdotV, k);
    float ggx2 = geometry_schlick_ggx(NdotL, k);
    return ggx1 * ggx2;
}

float3 compute_F0(float3 albedo, float metallic)
{
    float dielectric_F0 = 0.04; // defult reflectance value of the dielectric materials
    return lerp(dielectric_F0.xxx, albedo, metallic);
}

// @Normal, @ViewDir, @LightDir needs to be unit vectors
struct BRDFInput
{
    float Metallic;
    float Roughness;
    float3 Albedo;
    float3 Normal;
    float3 ViewDir;
    float3 LightDir;
};

float3 brdf(BRDFInput input)
{
    float3 half_vector = normalize(input.LightDir + input.ViewDir);

    float NdotL = max(dot(input.Normal, input.LightDir), 0);
    float NdotV = max(dot(input.Normal, input.ViewDir), 0);
    float NdotH = max(dot(input.Normal, half_vector), 0);

    float3 F0 = compute_F0(input.Albedo, input.Metallic);
    float3 F = fresnel(NdotL, F0);

    float D = gtr2(NdotH, input.Roughness);

    float k = (input.Roughness + 1) * (input.Roughness + 1) / 8;
    float G = geometry_smith(NdotL, NdotV, k);

    float3 Ks = F;
    float3 Kd = (1 - F) * (1 - input.Metallic);

    return Kd * input.Albedo * INV_PI + Ks * D * G / max((4 * NdotL * NdotV), 0.0001);
}

#endif