#ifndef H_GLOBAL
#define H_GLOBAL

#define PI 3.14159265359
#define INV_PI 0.31830988618

#define PREFILTER_ENVMAP_MIPMAP_SIZE 5u
#define CONSTANT_BUFFER_REGISTER_SHADER b0
#define CONSTANT_BUFFER_REGISTER_INSTANCE b1
#define CONSTANT_BUFFER_REGISTER_GLOBAL b2


SamplerState SamplerPointWrap           : register(s0);
SamplerState SamplerPointClamp          : register(s1);
SamplerState SamplerLinearWrap          : register(s2);
SamplerState SamplerLinearClamp         : register(s3);
SamplerState SamplerAnisotropicWrap     : register(s4);
SamplerState SamplerAnisotropicClamp    : register(s5);

struct SHCoefficientsPack
{
    float4 SHA_R;
    float4 SHB_R;
    float4 SHA_G;
    float4 SHB_G;
    float4 SHA_B;
    float4 SHB_B;
    float4 SHB_C;
};


cbuffer GlobalConstant : register(b2)
{
    SHCoefficientsPack SkyBoxSH;

    float4x4 InvView;
    float4x4 View;
    float4x4 Projection;
    float4x4 InvProjection;

    float3 CameraPos;
    float Ratio;

    float2 Resolution;
    float Near;
    float Far;

    float Fov;
    float DeltaTime;
    float Time;
}

// GBufferA R8G8B8A8
//  |---8-bits---||---8-bits---||---8-bits---||---8-bits---|
//  |---------------base color---------------||---unused---|


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

struct VSInput_P3F_N3F_T2F_T2F
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float3 tangent : TANGENT0;
    float3 color : COLOR0;
    float2 uv : TEXCOORD0;
};

struct VSInput_P3F_T2F
{
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
};

float3 decode_gamma(float3 color)
{
    return float3(pow(color.r, 2.2), pow(color.g, 2.2), pow(color.b, 2.2));
}

float3 encode_gamma(float3 color)
{
    // 0.454545 = 1 / 2.2
    return float3(pow(color.r, 0.454545), pow(color.g, 0.454545), pow(color.b, 0.454545));
}

float sign(float x)
{
    return  x < 0 ? -1 : 1;
}

float2 sign(float2 x)
{
    return float2(sign(x.x), sign(x.y));
}

float3 sign(float3 x)
{
    return float3(sign(x.x), sign(x.y), sign(x.z));
}

// ref: https://gamedev.stackexchange.com/questions/169508/octahedral-impostors-octahedral-mapping
float3 decode_octahedron(float2 uv)
{
    float3 dir = float3(uv * 2 - 1, 0);
    dir.z = 1 - abs(dir.x) - abs(dir.y);

    if(dir.z < 0)
    {
        // octahedron: |x| + |y| + |z| = 1
        // change xy to 1 - |y|, 1 - [x] if |x| + |y| > 1
        float3 absolute = abs(dir);
        dir.xy = sign(dir.xy) * float2(1.0f - abs(dir.y), 1.0f - abs(dir.x));
    }

    return dir;
}

float2 encode_octahedron(float3 dir)
{
    float sum = abs(dir.x) + abs(dir.y) + abs(dir.z);
    dir = dir / sum; // dir = dir / (|x| + ||y| + |z|), scale dir to octahedron surface

    if(dir.z < 0) {
        float3 absolute = abs(dir);
        dir.xy = sign(dir.xy) * float2(1.0f - abs(dir.y), 1.0f - abs(dir.x));
    }

    return dir.xy * 0.5 + 0.5;
}

float2 pack_normal(float3 normal)
{
    return encode_octahedron(normal);
}

float3 unpack_normal(float2 normal)
{
    return normalize(decode_octahedron(normal));
}
#endif