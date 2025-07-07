#include "Common.hlsli"

#define RootSig "CBV(b0, visibility=SHADER_VISIBILITY_GEOMETRY), " \
                "DescriptorTable(SRV(t0, numDescriptors = 4), visibility=SHADER_VISIBILITY_VERTEX)"

cbuffer PerFrameData : register(b0)
{
    float4x4 cProjection;
}

StructuredBuffer<AABB> tAABBs : register(t0);
StructuredBuffer<uint> tCompactedClusters : register(t1);
StructuredBuffer<uint2> tLightGrid : register(t2);
Texture2D tHeatmapTexture : register(t3);

struct GSInput
{
    float4 color : COLOR;
    float4 center : CENTER;
    float4 extents : EXTENTS;
    int lightCount : LIGHTCOUNT;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

[RootSignature(RootSig)]
GSInput VSMain(uint vertexId : SV_VertexID)
{
    GSInput output;
    
    uint clusterIndex = tCompactedClusters[vertexId];
    AABB aabb = tAABBs[clusterIndex];

    output.center = aabb.Center;
    output.extents = aabb.Extents;

    output.lightCount = tLightGrid[clusterIndex].y;
    output.color = tHeatmapTexture.Load(uint3(output.lightCount * 15, 0, 0));

    return output;
}

[maxvertexcount(16)]
void GSMain(point GSInput input[1], inout TriangleStream<PSInput> outStream)
{
    if (input[0].lightCount == 0)
    {
        return;
    }

    float4 center = input[0].center;
    float4 extents = input[0].extents;

    float4 positions[8] = {
        center + float4(-extents.x, -extents.y, -extents.z, 1.0f),
        center + float4(-extents.x, -extents.y, extents.z, 1.0f),
        center + float4(-extents.x, extents.y, -extents.z, 1.0f),
        center + float4(-extents.x, extents.y, extents.z, 1.0f),
        center + float4(extents.x, -extents.y, -extents.z, 1.0f),
        center + float4(extents.x, -extents.y, extents.z, 1.0f),
        center + float4(extents.x, extents.y, -extents.z, 1.0f),
        center + float4(extents.x, extents.y, extents.z, 1.0f)
    };

    uint indices[18] = {
        0, 1, 2,
        3, 6, 7,
        4, 5, -1,
        2, 6, 0,
        4, 1, 5,
        3, 7, -1
    };

    [unroll]
    for (int i = 0; i < 18; i++)
    {
        if (indices[i] == (uint)-1)
        {
            outStream.RestartStrip();
        }
        else
        {
            PSInput output;
            output.position = mul(positions[indices[i]], cProjection);
            output.color = input[0].color;
            outStream.Append(output);
        }
    }
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return float4(input.color.rgb, 0.2f);
}
