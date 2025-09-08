#include "Common.hlsli"

#define RootSig "CBV(b0, visibility = SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(SRV(t0, numDescriptors = 3)), " \
                "DescriptorTable(UAV(u0, numDescriptors = 1)) "
              
#define BLOCK_SIZE 16

Texture2D<float4> tInput : register(t0);
Texture2D<float> tDepth : register(t1);
RWTexture2D<float4> uOutput : register(u0);

cbuffer Data : register(b0)
{
    float4x4 cProjectionInverse;
    int4 cClusterDimensions;
    int2 cClusterSize;
    float2 cLightGridParams;
    float cNear;
    float cFar;
    float cFoV;
}

#if TILED_FORWARD
Texture2D<uint2> tLightGrid : register(t2);
#elif CLUSTERED_FORWARD
StructuredBuffer<uint2> tLightGrid : register(t2);
#endif

static float4 DEBUG_COLORS[] = {
    float4(0, 4, 141, 255) / 255.0f,
    float4(5, 10, 255, 255) / 255.0f,
    float4(0, 164, 255, 255) / 255.0f,
    float4(0, 244, 189, 255) / 255.0f,
    float4(0, 255, 41, 255) / 255.0f,
    float4(118, 253, 1, 255) / 255.0f,
    float4(255, 238, 0, 255) / 255.0f,
    float4(255, 86, 0, 255) / 255.0f,
    float4(204, 3, 0, 255) / 255.0f,
    float4(54, 0, 1, 255) / 255.0f
};

float EdgeDetection(uint2 index, uint width, uint height)
{
    float reference = LinearizeDepth(tDepth.Load(int3(index, 0)).r, cNear, cFar);
    uint2 offsets[8] = {
        uint2(-1, -1),
        uint2(-1, 0),
        uint2(-1, 1),
        uint2(0, -1),
        uint2(0, 1),
        uint2(1, -1),
        uint2(1, 0),
        uint2(1, 1)
    };

    float sampledValue = 0;
    for (int j = 0; j < 8; ++j)
    {
        sampledValue += LinearizeDepth(tDepth.Load(uint3(index + offsets[j], 0)), cNear, cFar);
    }
    sampledValue /= 8.0f;
    return lerp(1, 0, step(0.003f, length(reference - sampledValue)));
}

float InverseLerp(float value, float minValue, float maxValue)
{
    return (value - minValue) / (maxValue - minValue);
}

[RootSignature(RootSig)]
[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void DebugLightDensityCS(uint3 threadId : SV_DispatchThreadId)
{
    uint width, height;
    tInput.GetDimensions(width, height);
    if (threadId.x < width && threadId.y < height)
    {
#if TILED_FORWARD
        uint2 tileIndex = uint2(floor(threadId.xy / BLOCK_SIZE));
        uint lightCount = tLightGrid[tileIndex].y;
        uOutput[threadId.xy] = EdgeDetection(threadId.xy, width, height) * DEBUG_COLORS[min(lightCount, 9)];
#elif CLUSTERED_FORWARD
        
#if 1
        float fov = cFoV * 0.5f;
        float height = 300.0f;
        
        int2 pos = threadId.xy;
        pos.y = (720 - pos.y);
        pos -= uint2(1240, 720) * 0.5f;
        pos.y += 150.0;
        
        float angle = atan2(pos.x, pos.y);
        if (angle > -fov && angle < fov && pos.y < height)
        {
            float angleNormalized = InverseLerp(angle, -fov, fov);
            int widthSlice = floor(angleNormalized * cClusterDimensions.x);
            
            float viewDepth = (float) pos.y / height * (cNear - cFar) + cFar;
            uint slice = floor(log(viewDepth) * cLightGridParams.x - cLightGridParams.y);
            uint lightCount = 0;
            for (uint i = 0; i < cClusterDimensions.y; ++i)
            {
                uint3 clusterIndex3D = uint3(widthSlice, i, slice);
                uint clusterIndex1D = clusterIndex3D.x + (cClusterDimensions.x * (clusterIndex3D.y + cClusterDimensions.y * clusterIndex3D.z));
                lightCount = max(lightCount, tLightGrid[clusterIndex1D].y);
            }
            uOutput[threadId.xy] = float4(DEBUG_COLORS[min(9, lightCount)]);
        }
        else
#endif
        {
            float depth = tDepth.Load(uint3(threadId.xy, 0));
            float3 viewPos = ScreenToView(float4(0, 0, depth, 1), float2(1, 1), cProjectionInverse).xyz;
            uint slice = floor(log(viewPos.z) * cLightGridParams.x - cLightGridParams.y);
            uint3 clusterIndex3D = uint3(floor(threadId.xy / cClusterSize), slice);
            uint clusterIndex1D = clusterIndex3D.x + cClusterDimensions.x * (clusterIndex3D.y + clusterIndex3D.z * cClusterDimensions.y);
            uint lightCount = tLightGrid[clusterIndex1D].y;
            uOutput[threadId.xy] = EdgeDetection(threadId.xy, width, height) * DEBUG_COLORS[min(lightCount, 9)];        
        }
#endif
        
       
    }
}