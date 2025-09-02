#include "Common.hlsli"
#include "Lighting.hlsli"

#define BLOCK_SIZE 16

#define RootSig "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                "CBV(b0, visibility = SHADER_VISIBILITY_VERTEX), " \
                "CBV(b1, visibility = SHADER_VISIBILITY_ALL), " \
                "CBV(b2, visibility = SHADER_VISIBILITY_PIXEL), " \
                "DescriptorTable(SRV(t0, numDescriptors = 3), visibility = SHADER_VISIBILITY_PIXEL), " \
                "DescriptorTable(SRV(t3, numDescriptors = 5), visibility = SHADER_VISIBILITY_PIXEL), " \
                "StaticSampler(s0, filter = FILTER_ANISOTROPIC, maxAnisotropy = 4, visibility = SHADER_VISIBILITY_PIXEL), " \
                "StaticSampler(s1, filter = FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, visibility = SHADER_VISIBILITY_PIXEL, comparisonFunc = COMPARISON_GREATER)"

cbuffer PerObjectData : register(b0) // b-const buffer t-texture s-sampler
{
    float4x4 cWorld;
    float4x4 cWorldViewProjection;
}

cbuffer PerFrameData : register(b1)
{
    float4x4 cView;
    float4x4 cViewInverse;
    float4x4 cProjection;
    float2 cScreenDimensions;
    float cNearZ;
    float cFarZ;
#if CLUSTERED_FORWARD
    int4 cClusterDimensions;
    int2 cClusterSize;
    float cSliceMagicA;
    float cSliceMagicB;
#endif
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
    float3 positionWS : POSITION_WS;
    float3 positionVS : POSITION_VS;
    float2 texCoord : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : TEXCOORD1;
};

#if TILED_FORWARD
Texture2D<uint2> tLightGrid : register(t4);
#elif CLUSTERED_FORWARD
StructuredBuffer<uint2> tLightGrid : register(t4);
#endif

StructuredBuffer<uint> tLightIndexList : register(t5);

#if CLUSTERED_FORWARD
uint GetSliceFromDepth(float depth)
{
    return floor(cSliceMagicA * log(depth) - cSliceMagicB);
}
#endif

LightResult DoLight(float4 pos, float3 wPos, float3 vPos, float3 N, float3 V, float3 diffuseColor, float3 specularColor, float roughness)
{
#if TILED_FORWARD
    uint2 tileIndex = uint2(floor(pos.xy / BLOCK_SIZE));
    uint startOffset = tLightGrid[tileIndex].x;
    uint lightCount = tLightGrid[tileIndex].y;
#elif CLUSTERED_FORWARD
    uint3 clusterIndex3D = uint3(floor(pos.xy / cClusterSize), GetSliceFromDepth(vPos.z));
    uint clusterIndex1D = clusterIndex3D.x + cClusterDimensions.x * (clusterIndex3D.y + clusterIndex3D.z * cClusterDimensions.y);
    uint startOffset = tLightGrid[clusterIndex1D].x;
    uint lightCount = tLightGrid[clusterIndex1D].y; // lightCount = 0 means no light in this cluster
#endif

    LightResult totalResult = (LightResult)0;

    for (uint i = 0; i < lightCount; i++)
    {
        uint lightIndex = tLightIndexList[startOffset + i];
        Light light = tLights[lightIndex];

        LightResult result = DoLight(light, specularColor, diffuseColor, roughness, pos, wPos, vPos, N, V);
        totalResult.Diffuse += result.Diffuse;
        totalResult.Specular += result.Specular;
    }

    return totalResult;
}

[RootSignature(RootSig)]
PSInput VSMain(VSInput input)
{
    PSInput result;   
    result.position = mul(float4(input.position, 1.0f), cWorldViewProjection);
    result.texCoord = input.texCoord;
    result.normal = normalize(mul(input.normal, (float3x3)cWorld));
    result.tangent = normalize(mul(input.tangent, (float3x3)cWorld));
    result.bitangent = normalize(mul(input.bitangent, (float3x3)cWorld));
    result.positionWS = mul(float4(input.position, 1.0f), cWorld).xyz;
    result.positionVS = mul(float4(result.positionWS, 1.0f), cView).xyz;
    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 baseColor = tDiffuseTexture.Sample(sDiffuseSampler, input.texCoord);
    float3 specular = 0.5f;
    float metalness = 0;
    float r = 0.5f;

    float3 diffuseColor = ComputeDiffuseColor(baseColor.rgb, metalness);
    float3 specularColor = ComputeF0(specular.r, baseColor.rgb, metalness);

    float3x3 TBN = float3x3(normalize(input.tangent), normalize(input.bitangent), normalize(input.normal));
    float3 N = TangentSpaceNormalMapping(tNormalTexture, sDiffuseSampler, TBN, input.texCoord, true);
    float3 V = normalize(cViewInverse[3].xyz - input.positionWS);
    
    LightResult lightResults = DoLight(input.position, input.positionWS, input.positionVS, N, V, diffuseColor, specularColor, r);

    float3 color = lightResults.Diffuse + lightResults.Specular;

    // constant ambient
    float ao = tAO.Sample(sDiffuseSampler, (float2)input.position.xy / cScreenDimensions, 0).r; 
    color += ApplyAmbientLight(diffuseColor, ao, tLights[0].GetColor().rgb * 0.1f);

    color += ApplyVolumetricLighting(cViewInverse[3].xyz, input.positionWS.xyz, input.position.xyz, cView, tLights[0], 10);

    return float4(color, baseColor.a);
}

#if TILED_FORWARD
float4 DebugLightDensityPS(PSInput input) : SV_TARGET
{
    uint2 tileIndex = uint2(floor(input.position.xy / BLOCK_SIZE));
    uint startOffset = tLightGrid[tileIndex].x;
    uint lightCount = tLightGrid[tileIndex].y;
    return float4(((float)lightCount / 100).xxx, 1);
}
#endif