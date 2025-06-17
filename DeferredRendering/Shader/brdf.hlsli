#ifndef H_BRDF
#define H_BRDF

// defination of D F G term in brdf equation
// ref: https://learnopengl.com/PBR/Theory
float gtr2(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float t = (NdotH * NdotH) * (a * a - 1.0) + 1.0;
    return a * a / (PI * t * t);
}

float3 fresnel(float NdotL, float3 F0)
{
    return F0 + (1.0 - F0) * pow(1 - NdotL, 5);
}

float geometry_schlick_ggx(float NdotV, float k)
{
    return NdotV / (NdotV * (1.0 - k) + k);
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


// generate random microfacet normal
float3 ggx_important_sample(float roughness, float3 normal, float2 xi)
{
    // xi is uniform random number distributed in [0, 1]
    // ggx only matter to theta, so we will generate phi uniformly on [0, 2 * PI]
    // we will generate theta by substituting xi.y into inverse function of CDF(ggx(H) * NdotH), note: H is the microfacet normal, N is the surface normal.
    // ref: https://zhuanlan.zhihu.com/p/146144853 eq.29
    float a = roughness * roughness;
    float phi = 2.0 * PI * xi.x;
    float cos_theta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y ));
    float sin_theta = sqrt(1.0 - cos_theta * cos_theta );

    // sphereical coordinates to microfacet normal vector
    // assume (0, 0, 1) is the surface normal in tangent space
    float3 h;
    h.x = sin_theta * cos( phi );
    h.y = sin_theta * sin( phi );
    h.z = cos_theta;

    // transform @H from tangent space to world space
    // assume up_vector to be (0, 0, 1) if normal is not roughly parallel to (0, 0, 1), otherwise (1, 0, 0)
    // then we will apply cross product to up_vector and normal to generate tangent and bitangent
    float3 up_vector = abs(normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(normal, up_vector));
    float3 bitangent = cross(normal, tangent); // tangent and normal are orthogonal, so normalization can be omitted

    return normalize(tangent * h.x + bitangent * h.y + normal * h.z);
}

// Hammersley low-discrepancy sequences
// from: https://learnopengl.com/PBR/IBL/Specular-IBL
float RadicalInverse_VdC(uint bits) 
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

float2 hammersley(uint i, uint n)
{
    return float2(float(i)/float(n), RadicalInverse_VdC(i));
}  
#endif