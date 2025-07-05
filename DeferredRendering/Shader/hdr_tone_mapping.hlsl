#include "blit.hlsli"

// ACES tonemapping with auto exposure
// ref: https://bruop.github.io/tonemapping/

Texture2D LuminanceTexture : register(t0);
RWStructuredBuffer<float> AverageLuminance : register(u0);

float compute_max_luminance(float average_luminance)
{
    //ref: https://media.contentapi.ea.com/content/dam/eacom/frostbite/files/course-notes-moving-frostbite-to-pbr-v2.pdf section.5
    // relationship between average luminance and ev100, assume S = 100 and K = 12.5 for a typical camera setup
    // eq.1: ev100 = log2(Lavg * S / K)

    // then we find maximum luminance when luminance exposure at the maximum possible value
    // eq.2: H_sbs = 78 / S_sbs = Lmax * q * t / N^2
    // eq.3: ev100 = log2(N^2 / t)

    // combine eq.2 and eq.3, we get maximum luminance:
    // eq.4: Lmax = (78 * 2^ev100) / (S * q)

    // combine eq.1 and eq.4, we get maximum luminance through average luminance:
    // eq.5: Lmax = (78 * Lavg) / (K * q) = 9.6 * Lavg where K = 12.5, q = 0.65
    return 9.6 * average_luminance;
}

float3 ACES_tone_mapping(float3 x)
{
    // curve graph: https://www.desmos.com/calculator/riddeqyikw?lang=zh-CN
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x*(a*x+b))/(x*(c*x+d)+e));
}

float4 ps_main(PSInput input) : SV_TARGET
{
    float3 luminance = LuminanceTexture.Sample(SamplerPointClamp, input.uv).rgb;
    float gray_value = dot(luminance, float3(0.2126, 0.7152, 0.0722));
    float l_avg = AverageLuminance[0];

    // auto exposure, after this process, most part of the image will be in the range of [0, 1], except for those luminance values are above the @l_max
    float l_max = compute_max_luminance(l_avg);
    float3 exposed = luminance / (l_max + 0.001);

    // ACES tone mapping
    float3 mapped = ACES_tone_mapping(exposed);

    // gamma correction
    return float4(encode_gamma(mapped), 1);
}
