#include "Common.hlsl"

// raytracing output texture, accessed as a UAV
RWTexture2D<float4> gOutput : register(u0);

// raytracing acceleration structure, accessed as a SRV
RaytracingAccelerationStructure SceneBVH : register(t0);

[shader("raygeneration")]
void RayGen()
{
    // initialize the ray payload
    HitInfo payload;
    payload.colorAndDistance = float4(0.9, 0.6, 0.2, 1);

    // get the location within the dispatched 2D grid of work items
    // often maps to pixels, so this culd represent a pixel coordinate
    uint2 launchIndex = DispatchRaysIndex();

    gOutput[launchIndex] = float4(payload.colorAndDistance.rgb, 1.0f);
}