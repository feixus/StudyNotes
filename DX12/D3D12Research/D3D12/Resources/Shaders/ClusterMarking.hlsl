#include "Common.hlsli"
#include "CommonBindings.hlsli"

#define RootSig \
                "RootConstants(num32BitConstants = 2, b0), " \
                "CBV(b1, visibility = SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(SRV(t11, numDescriptors = 1)), " \
                "DescriptorTable(UAV(u1, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL), " \
                GLOBAL_BINDLESS_TABLE \
                "StaticSampler(s0, filter = FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_PIXEL)"

struct ObjectData
{
    uint Mesh;
    uint Material;
};

struct ViewData
{
    int4 ClusterDimensions;
    int2 ClusterSize;
    float2 LightGridParams;
    float4x4 View;
    float4x4 ViewProjection;
};

ConstantBuffer<ObjectData> cObjectData : register(b0);
ConstantBuffer<ViewData> cViewData : register(b1);

// UAV(register u#) and render target outputs(SV_Target#) share the same register namespace.
// SV_Target0 implicitly uses u0 internally.
RWStructuredBuffer<uint> uActiveCluster : register(u1);

// clusterCountZ * (log(depth) - log(n)) / log(f / n)
uint GetSliceFromDepth(float depth)
{
    return floor(log(depth) * cViewData.LightGridParams.x - cViewData.LightGridParams.y);
}

struct VSInput
{
    float3 position : POSITION;
    float2 texcoord : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : TEXCOORD1;
};

struct PSInput
{
    float4 position : SV_Position;
    float4 positionVS : TEXCOORD0;
    float2 texcoord : TEXCOORD1;
};

[RootSignature(RootSig)]
PSInput MarkClusters_VS(uint VertexId : SV_VertexID)
{
    PSInput output = (PSInput)0;
    MeshData mesh = tMeshes[cObjectData.Mesh];
    VSInput input = tBufferTable[mesh.VertexBuffer].Load<VSInput>(VertexId * sizeof(VSInput));
    float4 posWS = mul(float4(input.position, 1.0f), mesh.World);
    output.positionVS = mul(posWS, cViewData.View);
    output.position = mul(posWS, cViewData.ViewProjection);
    output.texcoord = input.texcoord;
    return output;
}

[earlydepthstencil]
void MarkClusters_PS(PSInput input)
{
    uint3 clusterIndex3D = uint3(floor(input.position.xy / cViewData.ClusterSize), GetSliceFromDepth(input.positionVS.z));
    uint clusterIndex1D = clusterIndex3D.x + cViewData.ClusterDimensions.x * (clusterIndex3D.y  + clusterIndex3D.z * cViewData.ClusterDimensions.y);
    uActiveCluster[clusterIndex1D] = 1;
}


