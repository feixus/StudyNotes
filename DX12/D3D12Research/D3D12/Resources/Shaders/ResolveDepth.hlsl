#include "Common.hlsl"

#ifndef DEPTH_RESOLVE_MIN
#define DEPTH_RESOLVE_MIN 1
#endif

#ifndef DEPTH_REAOLVE_MAX
#define DEPTH_RESOLVE_MAX 0
#endif

#ifndef DEPTH_RESOLVE_AVERAGE
#define DEPTH_RESOLVE_AVERAGE 0
#endif

Texture2DMS<float> tInputTexture : register(t0);
RWTexture2D<float> uOutputTexture : register(u0);

[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 dimensions;
    uint sampleCount;
    tInputTexture.GetDimensions(dimensions.x, dimensions.y, sampleCount);
    float result = 1;
    for (uint i = 0; i < sampleCount; i++)
    {
#if DEPTH_RESOLVE_MAX
        result = max(result, tInputTexture.Load(dispatchThreadID.xy, i).r);
#elif DEPTH_RESOLVE_MIN
        result = min(result, tInputTexture.Load(dispatchThreadID.xy, i).r);
#elif DEPTH_RESOLVE_AVERAGE
        result += tInputTexture.Load(dispatchThreadID.xy, i).r / sampleCount;
#endif
    }
	
    uOutputTexture[dispatchThreadID.xy] = result;
}