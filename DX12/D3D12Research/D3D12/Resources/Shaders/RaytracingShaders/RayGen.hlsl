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
    payload.colorAndDistance = float4(0, 0, 0, 0);

    // get the location within the dispatched 2D grid of work items
    // often maps to pixels, so this culd represent a pixel coordinate
    uint2 launchIndex = DispatchRaysIndex();
    float2 dims = float2(DispatchRaysDimensions().xy);
    float2 d = (((float2)launchIndex.xy + 0.5f) / dims.xy) * 2 + 1;
    
    RayDesc ray;
    ray.Origin = float3(d.x, -d.y, 1);
    ray.Direction = float3(0, 0, -1);
    ray.TMin = 0;
    ray.TMax = 1000;
    
    payload.colorAndDistance.xy = (float2)launchIndex.xy / dims.xy;

    /* trace the ray
    TraceRay(
        // parameter name: AccelerationStructure,  acceleration structure
        SceneBVH,
        
        // parameter name: RayFlags, specify the behavior upon hitting a surface
        RAY_FLAG_NONE,
        
        // parameter name: InstanceInclusionMask
        // instance inclusion mask used to mask out some geometry to this ray by and-ing the mask with a geometry mask
        // the 0xFF flag then indicates no geometry will be masked
        0xFF,
    
        // parameter name: RayContributionToHitGroupIndex
        // depending on the type of ray, a given object can have several hit groups attached
        //(ie. what to do when hitting to compute regular shading, and what to do when hitting to compute shadow)
        //these hit groups are specified sequentially in the SBT, so the value below indicates which offset (on 4 bits) to apply to the hit group for this ray
        // in this sample we only have one hit group per object, hence an offset of 0
        0,
    
        // parameter name: MultiplierForGeometryContributionToHitGroupIndex
        // the offsets in the SBT can be computed from the object ID, its instance ID, but also simply by the order the objects have beed pushed in the acceleration structure
        // this allows the application to group shaders in the SBT in the same order as they are added in the AS
        // in which case the value below represents the stride(4 bits representing the number of hit groups) between two consecutive objects.
        0,
    
        // parameter name: MissShaderIndex
        // index of the miss shader to use in case several consecutive miss shaders are present in the SBT.
        // this allows to change the behavior of the program when no geometry have been hit
        // for example one to return a sky color regular rendering, and another returning a full visibility value for shadow rays.
        // this sample has only one miss shader, hence an index 0
        0,
    
        // parameter name: Ray, ray information to trace
        ray,
        
        // parameter name : Payload
        // payload associated to the ray, which will be used to  communicate between the hit/miss shaders and the raygen
        payload
    );
    */
    gOutput[launchIndex] = float4(payload.colorAndDistance.rgb, 1.0f);
}