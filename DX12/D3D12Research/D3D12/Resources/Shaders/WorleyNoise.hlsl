#include "CommonBindings.hlsli"

#define RootSig ROOT_SIG("CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(UAV(u0, numDescriptors = 1), visibility=SHADER_VISIBILITY_ALL)")

struct PassParameters
{
    float4 Points[1024];    // feature points
    uint4 PointsPerRow[4];    // x/y/z: number of points per row in each axis
    uint Resolution;
};

ConstantBuffer<PassParameters> cPassParameters : register(b0);

RWTexture3D<float4> uOutputTexture : register(u0);

float WorleyNoise(float3 uvw, uint pointsPerRow)
{
    uvw *= pointsPerRow;
    float3 frc = frac(uvw);
    uint3 cell = floor(uvw);

    float minDistSq = 1;
    [loop]
    for (int offsetZ = -1; offsetZ <= 1; offsetZ++)
    {
        [loop]
        for (int offsetY = -1; offsetY <= 1; offsetY++)
        {
            [loop]
            for (int offsetX = -1; offsetX <= 1; offsetX++)
            {
                int3 offset = int3(offsetX, offsetY, offsetZ);
                int3 neighborCell = int3(cell + offset + pointsPerRow) % pointsPerRow;
                int pointIndex = neighborCell.x + pointsPerRow * (neighborCell.y + neighborCell.z * pointsPerRow);
                float3 p = cPassParameters.Points[pointIndex % 1024].xyz + offset;
                minDistSq = min(minDistSq, dot(frc - p, frc - p));
            }
        }
    }
    // return the closest feature point distance
    return sqrt(minDistSq);
}

[RootSignature(RootSig)]
[numthreads(8, 8, 8)]
void WorleyNoiseCS(uint3 threadId : SV_DispatchThreadID)
{
    float4 output = 0;
    float3 uvw = threadId.xyz / (float)cPassParameters.Resolution;
    for (int i = 0; i < 4; ++i)
    {
        float r = WorleyNoise(uvw, cPassParameters.PointsPerRow[i].x);
        float g = WorleyNoise(uvw, cPassParameters.PointsPerRow[i].y);
        float b = WorleyNoise(uvw, cPassParameters.PointsPerRow[i].z);
        float a = WorleyNoise(uvw, cPassParameters.PointsPerRow[i].w);
        float4 total = float4(r, g, b, a);
        float persistence = 0.5f;
        for (int j = 0; j < 4; ++j)
        {
            output[i] += total[i] * persistence;
            persistence *= persistence;
        }
    }
    uOutputTexture[threadId.xyz] = output;
}
