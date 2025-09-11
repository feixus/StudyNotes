cbuffer Constants : register(b0)
{
    float4 cPoints[2048];    // feature points
    uint4 cPointsPerRow;    // x/y/z: number of points per row in each axis
    uint Resolution;
};

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
                float3 p = cPoints[neighborCell.x + pointsPerRow * (neighborCell.y + neighborCell.z * pointsPerRow)].xyz + offset;
                minDistSq = min(minDistSq, dot(frc - p, frc - p));
            }
        }
    }
    // return the closest feature point distance
    return sqrt(minDistSq);
}

[numthreads(8, 8, 8)]
void WorleyNoiseCS(uint3 threadId : SV_DispatchThreadID)
{
    float3 uvw = threadId.xyz / (float)Resolution;
    float r = WorleyNoise(uvw, cPointsPerRow.x);
    float g = WorleyNoise(uvw, cPointsPerRow.y);
    float b = WorleyNoise(uvw, cPointsPerRow.z);
    float a = WorleyNoise(uvw, cPointsPerRow.a);
    uOutputTexture[threadId.xyz] = float4(r, g, b, a);
}
