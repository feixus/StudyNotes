#include "CommonBindings.hlsli"

#define RootSigVS ROOT_SIG("CBV(b100), " \
                "DescriptorTable(SRV(t0, numDescriptors = 3), visibility=SHADER_VISIBILITY_VERTEX)")

#define MAX_LIGHTS_PER_BUCKET 10

struct GSInput
{
    float4 Color : COLOR;
    float4 Center : CENTER;
    float4 Extents : EXTENTS;
    int LightCount : LIGHTCOUNT;
};

struct InterpolantsVSToPS
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR;
};

StructuredBuffer<AABB> tAABBs : register(t0);
StructuredBuffer<uint2> tLightGrid : register(t1);
Texture2D tHeatmapTexture : register(t2);

[RootSignature(RootSigVS)]
GSInput VSMain(uint vertexId : SV_VertexID)
{
    GSInput output;
    
    uint clusterIndex = vertexId;
    AABB aabb = tAABBs[clusterIndex];

    output.Center = aabb.Center;
    output.Extents = aabb.Extents;

    output.LightCount = tLightGrid[clusterIndex].y;
    output.Color = tHeatmapTexture.SampleLevel(sLinearClamp, float2((float)output.LightCount / MAX_LIGHTS_PER_BUCKET, 0), 0);

    return output;
}

[maxvertexcount(16)]
void GSMain(point GSInput input[1], inout TriangleStream<InterpolantsVSToPS> outStream)
{
    if (input[0].LightCount == 0)
    {
        return;
    }

    float4 center = input[0].Center;
    float4 extents = input[0].Extents;

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
            InterpolantsVSToPS output;
            output.Position = mul(positions[indices[i]], cViewUniforms.Projection);
            output.Color = input[0].Color;
            outStream.Append(output);
        }
    }
}

float4 PSMain(InterpolantsVSToPS input) : SV_Target
{
    return float4(input.Color.rgb, 0.2f);
}
