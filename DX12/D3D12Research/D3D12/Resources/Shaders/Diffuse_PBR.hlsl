#include "Common.hlsl"
#include "Lighting_PBR.hlsl"

#define RootSig "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                    "CBV(b0, visibility = SHADER_VISIBILITY_vERTEX), " \
                    "CBV(b1, visibility = SHADER_VISIBILITY_ALL), " \
                    "CBV(b2, visibility = SHADER_VISIBILITY_PIXEL), " \
                    "DescriptorTable(SRV(t0, numDescriptors = 3), visibility = SHADER_VISIBILITY_PIXEL), " \
                    "DescriptorTable(SRV(t3, numDescriptors = 4), visibility = SHADER_VISIBILITY_PIXEL), " \
                    "StaticSampler(s0, filter = FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_PIXEL), " \
                    "StaticSampler(s1, filter = FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_PIXEL)"

cbuffer PerObjectData : register(b0)
{
    float4x4 cWorld;
    float4x4 cWorldViewProjection;
}

cbuffer PerFrameData : register(b1)
{
    float4x4 cViewInverse;
}

struct VSInput
{
    float3 position : POSITION;
    float2 texCoord : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : TEXCOORD1;
}

struct PSInput
{
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : TEXCOORD1;
    float4 worldPosition : TEXCOORD2;
}

Texture2D tDiffuseTexture : register(t0);
SamplerState sDiffuseSampler : register(s0);

Texture2D tNormalTexture : register(t1);
SamplerState sNormalSampler : register(s1);

Texture2D tSpecularTexture : register(t2);

#if FORWARD_PLUS
Texture2D<uint2> tLightGrid : register(t4);
StructuredBuffer<uint> tLightIndexList : register(t5);
#endif

StructuredBuffer<Light> tLights : register(t6);

[RootSignature(RootSig)]
PSInput VSMain(VSInput input)
{
    PSInput output;
    output.position = mul(float4(input.position, 1), cWorldViewProjection);
    output.texCoord = input.texCoord;
    output.normal = normalize(mul(input.normal, (float3x3)cWorld));
    output.tangent = normalize(mul(input.tangent, (float3x3)cWorld));
    output.bitangent = normalize(mul(input.bitangent, (float3x3)cWorld));
    output.worldPosition = mul(float4(input.position, 1), cWorld);
    return output;
}

float3 CalculateNormal(float3 normal, float3 tangent, float3 bitangent, float2 texCoord, bool invertY)
{
    float3 normalMatrix = float3x3(tangent, bitangent, normal);
    float3 sampledNormal = tNormalTexture.Sample(sNormalSampler, texCoord).rgb;
    sampledNormal.xy = sampledNormal.xy * 2.0f - 1.0f;
    if (invertY)
    {
        sampledNormal.y = -sampledNormal.y;
    }
    sampledNormal = normalize(sampledNormal);
    return mul(sampledNormal, normalMatrix);
}

LightResult DoLight(float4 position, float3 worldPosition, float3 normal, float3 viewDirection, float3 albedo)
{
#if FORWARD_PLUS
    uint2 tileIndex = uint2(floor(position.xy / BLOCK_SIZE));
    uint startIndex = tLightGrid[tileIndex].x;
    uint lightCount = tLightGrid[tileIndex].y;
#else
    uint lightCount = cLightCount;
#endif

    LightResult totalResult = (LightResult)0;

#if DEBUG_VISUALIZE
    totalResult.Diffuse = (float)max(lightCount, 0) / 100.0f;
    return totalResult;
#endif

    for (uint i = 0; i < lightCount; i++)
    {
#if FORWARD_PLUS
        uint lightIndex = tLightIndexList[startIndex + i];
        Light light = tLights[lightIndex];
#else
        uint lightIndex = i;
        Light light = tLights[lightIndex];
        if (!light.Enabled)
        {
            continue;
        }
        if (light.Type != 0 && distance(worldPosition, light.Position) > light.Range)
        {
            continue;
        }
#endif

        LightResult result = DoLight(light, light.Color, albedo, 0.5f, worldPosition, normal, viewDirection);
        totalResult.Diffuse += result.Diffuse;
        totalResult.Specular += result.Specular;
    }

    return totalResult;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 lightPos = float4(100, 100, 100);

    float4 albedo = tDiffuseTexture.Sample(sDiffuseSampler, input.texCoord).rgb;
    float metalness = 0;
    float r = 1.0f; // 1 - tSpecularTexture.Sample(sDiffuseSampler, input.texCoord).r

    float3 N = CalculateNormal(normalize(input.normal), normalize(input.tangent), normalize(input.bitangent), input.texCoord, false);
    float3 V = normalize(cViewInverse[3].xyz - input.worldPosition.xyz);

    LightResult lighting = DoLight(input.position, input.worldPosition, N, V, albedo.rgb);

    float3 color = lighting.Diffuse + lighting.Specular;
    return float4(color, albedo.a)
}