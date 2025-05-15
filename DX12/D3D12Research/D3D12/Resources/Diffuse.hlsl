#include "Constants.hlsl"
#include "Common.hlsl"
#include "Lighting.hlsl"

cbuffer PerObjectData : register(b0) // b-const buffer t-texture s-sampler
{
    float4x4 cWorld;
    float4x4 cWorldViewProjection;
}

cbuffer PerFrameData : register(b1)
{
    float4x4 cViewInverse;
}

Texture2D myDiffuseTexture : register(t0);
SamplerState myDiffuseSampler : register(s0);

Texture2D myNormalTexture : register(t1);
SamplerState myNormalSampler : register(s1);

Texture2D mySpecularTexture : register(t2);

#if FORWARD_PLUS
Texture2D<uint2> tLightGrid : register(t4);
StructuredBuffer<uint> tLightIndexList : register(t5);
#endif

StructuredBuffer<Light> Lights : register(t6);

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
    float4 worldPosition : TEXCOORD2;
};

LightResult DoLight(float4 position, float3 worldPosition, float3 normal, float3 viewDirection)
{
#if FORWARD_PLUS
    uint2 tileIndex = uint2(floor(position.xy / BLOCK_SIZE));
    uint startOffset = tLightGrid[tileIndex].x;
    uint lightCount = tLightGrid[tileIndex].y;
#else
    uint lightCount = LIGHT_COUNT;
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

        if (light.Type != 0 && distance(worldPosition, light.Position) > light.Range)
        {
            continue;
        }
#endif

        LightResult result;

        switch(light.Type)
        {
        case LIGHT_DIRECTIONAL:
            result = DoDirectionalLight(light, worldPosition, normal, viewDirection);
            break;
        case LIGHT_POINT:
            result = DoPointLight(light, worldPosition, normal, viewDirection);
            break;
        case LIGHT_SPOT:
            result = DoSpotLight(light, worldPosition, normal, viewDirection);
            break;
        default:
            result.Diffuse = float4(1, 0, 1, 1);
            result.Specular = float4(0, 0, 0, 1);
            break;
        }
        
        totalResult.Diffuse += result.Diffuse;
        totalResult.Specular += result.Specular;
    }

    return totalResult;
}

float3 CalculateNormal(float3 normal, float3 tangent, float3 bitangent, float2 texCoord, bool invertY)
{
    float3x3 normalMatrix = float3x3(tangent, bitangent, normal);
    float3 sampleNormal = myNormalTexture.Sample(myNormalSampler, texCoord).rgb;
    sampleNormal.xy = sampleNormal.xy * 2.0f - 1.0f;
    if (invertY)
    {
        sampleNormal.y = -sampleNormal.y;
    }
    sampleNormal = normalize(sampleNormal);
    return mul(sampleNormal, normalMatrix);
}

PSInput VSMain(VSInput input)
{
    PSInput result;
    
    result.position = mul(float4(input.position, 1.0f), cWorldViewProjection);
    result.texCoord = input.texCoord;
    result.normal = normalize(mul(input.normal, (float3x3)cWorld));
    result.tangent = normalize(mul(input.tangent, (float3x3)cWorld));
    result.bitangent = normalize(mul(input.bitangent, (float3x3)cWorld));
    result.worldPosition = mul(float4(input.position, 1.0f), cWorld);
    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 diffuseSample = myDiffuseTexture.Sample(myDiffuseSampler, input.texCoord);
   
    float3 viewDirection = normalize(input.worldPosition.xyz - cViewInverse[3].xyz);
    float3 normal = CalculateNormal(normalize(input.normal), normalize(input.tangent), normalize(input.bitangent), input.texCoord, true);
    
    LightResult lightResults = DoLight(input.position, input.worldPosition.xyz, input.normal, viewDirection);

    float4 specularSample = mySpecularTexture.Sample(myDiffuseSampler, input.texCoord);
    lightResults.Specular *= specularSample;
#if !DEBUG_VISUALIZE
    lightResults.Diffuse *= diffuseSample;
#endif

    float4 color = saturate(lightResults.Diffuse + lightResults.Specular);
    color.a = diffuseSample.a;

    return color;
}