#include "Common.hlsli"

#define RootSig "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                "CBV(b0, visibility = SHADER_VISIBILITY_VERTEX), " \
                "DescriptorTable(SRV(t0, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL), " \
                "StaticSampler(s0, filter = FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_PIXEL)"

cbuffer PerObjectData : register(b0)
{
    float4x4 World;
    float4x4 WorldViewProjection;
}

Texture2D tAlphaTexture: register(t0);
SamplerState sAlphaSampler : register(s0);

struct VSInput
{
    float3 position : POSITION;
    float2 texCoord : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : TEXCOORD1;
};

struct PSInput
{
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : TEXCOORD1;
};

[RootSignature(RootSig)]
PSInput VSMain(VSInput input)
{
    PSInput result = (PSInput) 0;
    result.position = mul(float4(input.position, 1.0f), WorldViewProjection);
    result.texCoord = input.texCoord;
    result.normal = normalize(mul(float4(input.normal, 0.0f), World).xyz);
    result.tangent = normalize(mul(float4(input.tangent, 0.0f), World).xyz);
    result.bitangent = normalize(mul(float4(input.bitangent, 0.0f), World).xyz);
    return result;
}

float4 PSMain(PSInput input) : SV_Target
{
    // if (tAlphaTexture.Sample(sAlphaSampler, input.texCoord).a == 0.0f)
    // {
    //     discard;
    // }
    return float4(normalize(input.normal), 1.0f);
}