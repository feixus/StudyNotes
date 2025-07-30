#include "Common.hlsli"
#include "Lighting.hlsli"

#define RootSig "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                "CBV(b0, visibility = SHADER_VISIBILITY_VERTEX), " \
                "CBV(b1, visibility = SHADER_VISIBILITY_ALL), " \
                "CBV(b2, visibility = SHADER_VISIBILITY_PIXEL), " \
                "DescriptorTable(SRV(t0, numDescriptors = 3)), " \
                "DescriptorTable(SRV(t3, numDescriptors = 5), visibility = SHADER_VISIBILITY_PIXEL), " \
                "StaticSampler(s0, filter = FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_PIXEL), " \
                "StaticSampler(s1, filter = FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, visibility = SHADER_VISIBILITY_PIXEL, comparisonFunc = COMPARISON_GREATER)"

cbuffer PerObjectData : register(b0) // b-const buffer t-texture s-sampler
{
    float4x4 cWorld;
    float4x4 cWorldViewProjection;
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
Texture2D mySpecularTexture : register(t2);

SamplerState myDiffuseSampler : register(s0);

StructuredBuffer<uint2> tLightGrid : register(t4);
StructuredBuffer<uint> tLightIndexList : register(t5);
StructuredBuffer<Light> Lights : register(t6);
Texture2D tAO : register(t7);

uint GetSliceFromDepth(float depth)
{
    return (uint)(cSliceMagicA * log(depth) - cSliceMagicB);
}

int GetLightCount(float4 vPos, float4 wPos)
{
    uint zSlice = GetSliceFromDepth(vPos.z);
    uint2 clusterIndexXY = floor(wPos.xy / cClusterSize);
    uint clusterIndex1D = clusterIndexXY.x + clusterIndexXY.y * cClusterDimensions.x + zSlice * cClusterDimensions.x * cClusterDimensions.y;
    return tLightGrid[clusterIndex1D].y;
}

LightResult DoLight(float4 pos, float4 vPos, float3 wPos, float3 N, float3 V, float3 diffuseColor, float3 specularColor, float roughness)
{
    uint3 clusterIndex3D = uint3(floor(pos.xy / cClusterSize), GetSliceFromDepth(vPos.z));
    uint clusterIndex1D = clusterIndex3D.x + cClusterDimensions.x * (clusterIndex3D.y + clusterIndex3D.z * cClusterDimensions.y);

    uint startOffset = tLightGrid[clusterIndex1D].x;
    uint lightCount = tLightGrid[clusterIndex1D].y; // lightCount = 0 means no light in this cluster
    LightResult totalResult = (LightResult)0;

    for (uint i = 0; i < lightCount; i++)
    {
        uint lightIndex = tLightIndexList[startOffset + i];
        Light light = Lights[lightIndex];

        LightResult result = DoLight(light, specularColor, diffuseColor, roughness, wPos, N, V);
        totalResult.Diffuse += result.Diffuse * light.Color.rgb * light.Color.a;
        totalResult.Specular += result.Specular * light.Color.rgb * light.Color.a;
    }

    return totalResult;
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

[RootSignature(RootSig)]
PSInput VSMain(VSInput input)
{
    PSInput result;
    
    result.positionWS = mul(float4(input.position, 1.0f), cWorld);
    result.positionVS = mul(result.positionWS, cView);
    result.position = mul(float4(input.position, 1.0f), cWorldViewProjection);
    result.texCoord = input.texCoord;
    result.normal = normalize(mul(input.normal, (float3x3)cWorld));
    result.tangent = normalize(mul(input.tangent, (float3x3)cWorld));
    result.bitangent = normalize(mul(input.bitangent, (float3x3)cWorld));

    return result;
}

[earlydepthstencil]  // explicitly requests early depth stencil
float4 PSMain(PSInput input) : SV_TARGET
{
    float4 baseColor = myDiffuseTexture.Sample(myDiffuseSampler, input.texCoord);
    float3 specular = 1;
    float metalness = 0;
    float r = lerp(0.3f, 1.0f, 1 - mySpecularTexture.Sample(myDiffuseSampler, input.texCoord).r);

    float3 diffuseColor = baseColor.rgb * (1 - metalness);
    float3 specularColor = ComputeF0(specular.r, baseColor.rgb, metalness);

    float3 N = CalculateNormal(normalize(input.normal), normalize(input.tangent), normalize(input.bitangent), input.texCoord, false);
    float3 V = normalize(cViewInverse[3].xyz - input.positionWS.xyz);
    
    LightResult lightResults = DoLight(input.position, input.positionVS, input.positionWS.xyz, N, V, diffuseColor, specularColor, r);

    float3 color = lightResults.Diffuse + lightResults.Specular;
    float ao = tAO.Sample(myDiffuseSampler, (float2)input.position.xy / cScreenDimensions).r;
    color += ApplyAmbientLight(diffuseColor, ao, 0.1f);
    
    return float4(color, baseColor.a);
}