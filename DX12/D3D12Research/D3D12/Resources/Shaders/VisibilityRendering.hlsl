#include "Common.hlsli"
#include "CommonBindings.hlsli"
#include "Random.hlsli"

#define RootSig "RootConstants(num32BitConstants = 2, b0), " \
                "CBV(b1, visibility = SHADER_VISIBILITY_VERTEX), " \
                "DescriptorTable(SRV(t10, numDescriptors = 2)), " \
                GLOBAL_BINDLESS_TABLE \
                "StaticSampler(s0, filter = FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_PIXEL)"

struct PerObjectData
{
    uint Mesh;
    uint Material;
};

struct PerViewData
{
    float4x4 ViewProjection;
};

ConstantBuffer<PerObjectData> cObjectData : register(b0);
ConstantBuffer<PerViewData> cViewData : register(b1);

struct Vertex
{
    uint2 Position;
    uint UV;
    float3 Normal;
    float4 Tangent;
};

struct PSInput
{
    float4 Position : SV_Position;
};

[RootSignature(RootSig)]
PSInput VSMain(uint VertexId : SV_VertexID)
{
    PSInput result = (PSInput) 0;
    MeshData mesh = tMeshes[cObjectData.Mesh];
    Vertex input = tBufferTable[mesh.VertexBuffer].Load<Vertex>(VertexId * sizeof(Vertex));
    result.Position = mul(mul(float4(UnpackHalf3(input.Position), 1.0f), mesh.World), cViewData.ViewProjection);
    return result;
}

void PSMain(
    PSInput input,
    uint primitiveIndex : SV_PrimitiveID,
    float3 barycentrics : SV_Barycentrics,
    out uint outPrimitiveMask : SV_Target0,
    out float2 outBarycentrics : SV_Target1)
{
    MaterialData material = tMaterials[cObjectData.Material];
    outPrimitiveMask = (cObjectData.Mesh << 16) | (primitiveIndex & 0xFFFF);
    outBarycentrics = barycentrics.xy;
}
