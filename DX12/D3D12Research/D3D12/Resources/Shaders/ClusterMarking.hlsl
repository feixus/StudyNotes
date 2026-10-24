#include "CommonBindings.hlsli"

#define RootSig ROOT_SIG("RootConstants(num32BitConstants = 2, b0), " \
                "CBV(b1, visibility = SHADER_VISIBILITY_ALL), " \
                "CBV(b100, visibility = SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(SRV(t11, numDescriptors = 1)), " \
                "DescriptorTable(UAV(u1, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL)")

struct ObjectData
{
    uint Mesh;
    uint Material;
};

struct PassData
{
    int4 ClusterDimensions;
    int2 ClusterSize;
    float2 LightGridParams;
};

ConstantBuffer<ObjectData> cObjectData : register(b0);
ConstantBuffer<PassData> cPassData : register(b1);

// UAV(register u#) and render target outputs(SV_Target#) share the same register namespace.
// SV_Target0 implicitly uses u0 internally.
RWStructuredBuffer<uint> uActiveCluster : register(u1);

// clusterCountZ * (log(depth) - log(n)) / log(f / n)
uint GetSliceFromDepth(float depth)
{
    return floor(log(depth) * cPassData.LightGridParams.x - cPassData.LightGridParams.y);
}

struct Vertex
{
    uint2 position;
    uint texcoord;
    float3 normal;
    float4 tangent;
};

struct PSInput
{
    float4 position : SV_Position;
    float4 positionVS : TEXCOORD0;
};

[RootSignature(RootSig)]
PSInput MarkClusters_VS(uint VertexId : SV_VertexID)
{
    PSInput output = (PSInput)0;
    MeshData mesh = tMeshes[cObjectData.Mesh];
    Vertex input = tBufferTable[mesh.VertexBuffer].Load<Vertex>(VertexId * sizeof(Vertex));
    float4 posWS = mul(float4(UnpackHalf3(input.position), 1.0f), mesh.World);
    output.positionVS = mul(posWS, cViewUniforms.View);
    output.position = mul(posWS, cViewUniforms.ViewProjection);
    return output;
}

[earlydepthstencil]
void MarkClusters_PS(PSInput input)
{
    uint3 clusterIndex3D = uint3(floor(input.position.xy / cPassData.ClusterSize), GetSliceFromDepth(input.positionVS.z));
    uint clusterIndex1D = clusterIndex3D.x + cPassData.ClusterDimensions.x * (clusterIndex3D.y  + clusterIndex3D.z * cPassData.ClusterDimensions.y);
    uActiveCluster[clusterIndex1D] = 1;
}


