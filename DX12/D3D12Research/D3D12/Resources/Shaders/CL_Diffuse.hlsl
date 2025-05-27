#include "Constants.hlsl"
#include "Common.hlsl"
#include "Lighting.hlsl"

cbuffer PerObjectData : register(b0) // b-const buffer t-texture s-sampler
{
    float4x4 cWorld;
}

cbuffer PerFrameData : register(b1)
{
    float4x4 cView;
    float4x4 cProjection;
    float4x4 cViewInverse;
    uint4 cClusterDimensions;
    float2 cScreenDimensions;
    float cNearZ;
    float cFarZ;
    float2 cClusterSize;
    float cSliceMagicA;
    float cSliceMagicB;
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
    float4 positionVS : POSITION_VS;
    float4 positionWS : POSITION_WS;
    float2 texCoord : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : TEXCOORD1;
};

Texture2D myDiffuseTexture : register(t0);
Texture2D myNormalTexture : register(t1);

SamplerState myDiffuseSampler : register(s0);
SamplerState myNormalSampler : register(s1);

Texture2D mySpecularTexture : register(t2);
StructuredBuffer<uint2> tLightGrid : register(t3);
StructuredBuffer<uint> tLightIndexList : register(t4);
StructuredBuffer<Light> Lights : register(t5);
Texture2D tHeatMapTexture : register(t6);


uint GetSliceFromDepth(float depth)
{
    return (uint)(cSliceMagicA * log(depth) - cSliceMagicB);
}

int GetLightCount(float4 positionVS, float4 position)
{
    uint zSlice = GetSliceFromDepth(positionVS.z);
    uint2 clusterIndexXY = floor(position.xy / cClusterSize);
    uint clusterIndex1D = clusterIndexXY.x + clusterIndexXY.y * cClusterDimensions.x + zSlice * cClusterDimensions.x * cClusterDimensions.y;
    return tLightGrid[clusterIndex1D].y;
}

LightResult DoLight(float4 position, float4 viewSpacePosition, float3 worldPosition, float3 normal, float3 viewDirection)
{
    uint zSlice = GetSliceFromDepth(viewSpacePosition.z);
    uint2 clusterIndexXY = floor(position.xy / cClusterSize);
    uint clusterIndex1D = clusterIndexXY.x + clusterIndexXY.y * cClusterDimensions.x + zSlice * cClusterDimensions.x * cClusterDimensions.y;

    uint startOffset = tLightGrid[clusterIndex1D].x;
    uint lightCount = tLightGrid[clusterIndex1D].y;
    LightResult totalResult = (LightResult)0;

    for (uint i = 0; i < lightCount; i++)
    {
        uint lightIndex = tLightIndexList[startOffset + i];
        Light light = Lights[lightIndex];

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

PSInput Diffuse_VS(VSInput input)
{
    PSInput result;
    
    result.positionWS = mul(float4(input.position, 1.0f), cWorld);
    result.positionVS = mul(result.positionWS, cView);
    result.position = mul(result.positionVS, cProjection);
    result.texCoord = input.texCoord;
    result.normal = normalize(mul(input.normal, (float3x3)cWorld));
    result.tangent = normalize(mul(input.tangent, (float3x3)cWorld));
    result.bitangent = normalize(mul(input.bitangent, (float3x3)cWorld));

    return result;
}

float4 Diffuse_PS(PSInput input) : SV_TARGET
{
    // float2 uv = float2((float)GetLightCount(input.positionVS, input.positionWS) / 100.0f, 0.0f);
    // return tHeatMapTexture.Sample(myDiffuseSampler, uv);

    float4 diffuseSample = myDiffuseTexture.Sample(myDiffuseSampler, input.texCoord);
   
    float3 viewDirection = normalize(input.positionWS.xyz - cViewInverse[3].xyz);
    float3 normal = CalculateNormal(normalize(input.normal), normalize(input.tangent), normalize(input.bitangent), input.texCoord, true);
    
    LightResult lightResults = DoLight(input.position, input.positionVS, input.positionWS.xyz, input.normal, viewDirection);

    float4 specularSample = mySpecularTexture.Sample(myDiffuseSampler, input.texCoord);
    lightResults.Specular *= specularSample;
    lightResults.Diffuse *= diffuseSample;

    float4 color = saturate(lightResults.Diffuse + lightResults.Specular);
    color.a = diffuseSample.a;
    
    return color;
}