#include "Common.hlsli"

#define RootSig "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                "CBV(b0, visibility = SHADER_VISIBILITY_ALL), " \
                "CBV(b1, visibility = SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(UAV(u1, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL), " \
                "DescriptorTable(SRV(t0, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL), " \
                "StaticSampler(s0, filter = FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_PIXEL)"

cbuffer PerObjectParameters : register(b0)
{
    float4x4 cWorldView;
    float4x4 cWorldViewProjection;
}

cbuffer Parameters : register(b1)
{
    uint4 cClusterDimensions;
    float2 cClusterSize;
    float cSliceMagicA;
    float cSliceMagicB;
}

// UAV(register u#) and render target outputs(SV_Target#) share the same register namespace.
// SV_Target0 implicitly uses u0 internally.
RWStructuredBuffer<uint> uActiveCluster : register(u1);

Texture2D tDiffuseTexture : register(t0);
SamplerState sDiffuseSampler : register(s0);

uint GetSliceFromDepth(float depth)
{
    return floor(log(depth) * cSliceMagicA - cSliceMagicB);
}

struct VS_Input
{
    float3 position : POSITION;
    float2 texcoord : TEXCOORD;
};

struct PS_Input
{
    float4 position : SV_Position;
    float4 positionVS : TEXCOORD0;
    float2 texcoord : TEXCOORD1;
};

[RootSignature(RootSig)]
PS_Input MarkClusters_VS(VS_Input input)
{
    PS_Input output = (PS_Input)0;
    output.positionVS = mul(float4(input.position, 1.0f), cWorldView);
    output.position = mul(float4(input.position, 1.0f), cWorldViewProjection);
    output.texcoord = input.texcoord;
    return output;
}

[earlydepthstencil]
void MarkClusters_PS(PS_Input input)
{
    uint zSlice = GetSliceFromDepth(input.positionVS.z);
    uint2 clusterIndexXY = floor(input.position.xy / cClusterSize);
    uint clusterIndex1D = clusterIndexXY.x + cClusterDimensions.x * (clusterIndexXY.y  + zSlice * cClusterDimensions.y);
    uActiveCluster[clusterIndex1D] = 1;
}


