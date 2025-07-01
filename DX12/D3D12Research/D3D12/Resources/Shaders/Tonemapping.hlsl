#define RootSig "DescriptorTable(SRV(t0, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL), " \
                "StaticSampler(s0, filter = FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_PIXEL)"

struct PSInput
{
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD;
};

Texture2D tColorTexture : register(t0);
SamplerState sColorSampler : register(s0);

[RootSignature(RootSig)]
PSInput VSMain(uint index : SV_VertexID)
{
    PSInput output;
    output.position.x = (float)(index / 2) * 4.0f - 1.0f;
    output.position.y = (float)(index % 2) * 4.0f - 1.0f;
    output.position.z = 0.0f;
    output.position.w = 1.0f;

    output.texCoord.x = (float)(index / 2) * 2.0f;
    output.texCoord.y = 1.0f - (float)(index % 2) * 2.0f;

    return output;
}

#define TONEMAP_GAMMA 1.0

// Reinhard tonemapping
float4 tonemap_reinhard(in float3 color)
{
    color *= 16;
    color = color / (1 + color);
    float3 ret = pow(color, TONEMAP_GAMMA);
    return float4(ret, 1.0f);
}

// Uncharted 2 tonemapping
float3 tonemap_uncharted2(in float3 x)
{
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;

    return (x * (A * x + C * B) + D * E) / (x * (A * x + B) + F * D) - E / F;
}

float3 tonemap_uc2(in float3 color)
{
    float W = 11.2;
    color *= 16; // hardcoded exposure adjustment

    float exposure_bias = 2.0f;
    float3 curr = tonemap_uncharted2(color * exposure_bias);

    float3 white_scale = 1.0f / tonemap_uncharted2(W);
    float3 ccolor = curr * white_scale;

    return pow(abs(ccolor), TONEMAP_GAMMA); // gamma
}

float3 tonemap_filmic(in float3 color)
{
    color = max(0, color - 0.004f);
    color = (color * (6.2f * color + 0.5f)) / (color * (6.2f * color + 1.7f) + 0.06f);

    // result has 1/2.2 baked in
    return pow(color, TONEMAP_GAMMA);
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 color = tonemap_uc2(tColorTexture.Sample(sColorSampler, input.texCoord).rgb);
    return float4(color, 1.0f);
}