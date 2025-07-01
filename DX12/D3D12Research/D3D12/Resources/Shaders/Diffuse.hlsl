#include "Common.hlsli"
#include "Lighting.hlsli"

#define RootSig "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                "CBV(b0, visibility = SHADER_VISIBILITY_VERTEX), " \
                "CBV(b1, visibility = SHADER_VISIBILITY_ALL), " \
                "CBV(b2, visibility = SHADER_VISIBILITY_PIXEL), " \
                "DescriptorTable(SRV(t0, numDescriptors = 3), visibility = SHADER_VISIBILITY_PIXEL), " \
                "DescriptorTable(SRV(t3, numDescriptors = 4), visibility = SHADER_VISIBILITY_PIXEL), " \
                "StaticSampler(s0, filter = FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_PIXEL), " \
                "StaticSampler(s1, filter = FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, visibility = SHADER_VISIBILITY_PIXEL, comparisonFunc = COMPARISON_GREATER)"

cbuffer PerObjectData : register(b0) // b-const buffer t-texture s-sampler
{
    float4x4 cWorld;
    float4x4 cWorldViewProjection;
}

cbuffer PerFrameData : register(b1)
{
    float4x4 cViewInverse;
    int cLightCount;
}

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
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : TEXCOORD1;
    float3 worldPosition : TEXCOORD2;
};

Texture2D myDiffuseTexture : register(t0);
Texture2D myNormalTexture : register(t1);
Texture2D mySpecularTexture : register(t2);

SamplerState myDiffuseSampler : register(s0);

#if FORWARD_PLUS
Texture2D<uint2> tLightGrid : register(t4);
StructuredBuffer<uint> tLightIndexList : register(t5);
#endif

StructuredBuffer<Light> Lights : register(t6);

[RootSignature(RootSig)]
PSInput VSMain(VSInput input)
{
    PSInput result;   
    result.position = mul(float4(input.position, 1.0f), cWorldViewProjection);
    result.texCoord = input.texCoord;
    result.normal = normalize(mul(input.normal, (float3x3)cWorld));
    result.tangent = normalize(mul(input.tangent, (float3x3)cWorld));
    result.bitangent = normalize(mul(input.bitangent, (float3x3)cWorld));
    result.worldPosition = mul(float4(input.position, 1.0f), cWorld).xyz;
    return result;
}

float3 CalculateNormal(float3 N, float3 T, float3 BT, float2 texCoord, bool invertY)
{
    float3x3 normalMatrix = float3x3(T, BT, N);
    float3 sampleNormal = myNormalTexture.Sample(myDiffuseSampler, texCoord).rgb;
    sampleNormal.xy = sampleNormal.xy * 2.0f - 1.0f;
    if (invertY)
    {
        sampleNormal.y = -sampleNormal.y;
    }
    sampleNormal = normalize(sampleNormal);
    return mul(sampleNormal, normalMatrix);
}

LightResult DoLight(float4 pos, float3 wPos, float3 N, float3 V, float3 diffuseColor, float3 specularColor, float roughness)
{
#if FORWARD_PLUS
    uint2 tileIndex = uint2(floor(pos.xy / BLOCK_SIZE));
    uint startOffset = tLightGrid[tileIndex].x;
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
        uint lightIndex = tLightIndexList[startOffset + i];
        Light light = Lights[lightIndex];
#else
        uint lightIndex = i;
        Light light = Lights[i];

        if (light.Enabled == 0)
        {
            continue;
        }

        if (light.Type != 0 && distance(wPos, light.Position) > light.Range)
        {
            continue;
        }
#endif

        LightResult result = DoLight(light, specularColor, diffuseColor, roughness, wPos, N, V);

        totalResult.Diffuse += result.Diffuse * light.Color.rgb * light.Color.a;
        totalResult.Specular += result.Specular * light.Color.rgb * light.Color.a;
    }

    return totalResult;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 baseColor = myDiffuseTexture.Sample(myDiffuseSampler, input.texCoord);
    float3 specular = 1.0f;
    float metalness = 0;
    float r = lerp(0.3f, 1.0f, 1 - mySpecularTexture.Sample(myDiffuseSampler, input.texCoord).r);

    float3 diffuseColor = baseColor.rgb * (1 - metalness);
    float3 specularColor = ComputeF0(specular.r, baseColor.rgb, metalness);

    float3 N = CalculateNormal(normalize(input.normal), normalize(input.tangent), normalize(input.bitangent), input.texCoord, false);
    float3 V = normalize(cViewInverse[3].xyz - input.worldPosition.xyz);
    
    LightResult lightResults = DoLight(input.position, input.worldPosition, N, V, diffuseColor, specularColor, r);

    float3 color = lightResults.Diffuse + lightResults.Specular;
    return float4(color, baseColor.a);
}