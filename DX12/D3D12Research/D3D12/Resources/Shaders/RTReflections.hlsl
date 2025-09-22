#include "Common.hlsli"

RWTexture2D<float4> gOutput : register(u0);

RaytracingAccelerationStructure SceneBVH : register(t0);

Texture2D tDepth : register(t1);

SamplerState sSceneSampler : register(s0);

struct Vertex
{
    float3 position;
    float2 texCoord;
    float3 normal;
    float3 tangent;
    float3 bitangent;
};

ByteAddressBuffer tVertexData : register(t100);
ByteAddressBuffer tIndexData : register(t101);

cbuffer HitData : register(b1)
{
    int DiffuseIndex;
    int NormalIndex;
    int RoughnessIndex;
    int MetallicIndex;
}

Texture2D tMaterialTextures[] : register(t200);

cbuffer ShaderParameters : register(b0)
{
    float4x4 cViewInverse;
    float4x4 cViewProjectionInverse;
}

struct RayPayload
{
    float3 output;
};

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attrib)
{
    float3 b = float3(1.0f - attrib.barycentrics.x - attrib.barycentrics.y, attrib.barycentrics.x, attrib.barycentrics.y);
    uint3 indices = tIndexData.Load3(PrimitiveIndex() * sizeof(uint3));
    Vertex v0 = tVertexData.Load<Vertex>(indices.x * sizeof(Vertex));
    Vertex v1 = tVertexData.Load<Vertex>(indices.y * sizeof(Vertex));
    Vertex v2 = tVertexData.Load<Vertex>(indices.z * sizeof(Vertex));
    float2 texCoord = v0.texCoord * b.x + v1.texCoord * b.y + v2.texCoord * b.z;
    float3 normal = v0.normal * b.x + v1.normal * b.y + v2.normal * b.z;
    float NoL = dot(normal, normalize(float3(1, -1, 1)));

    float3 color = tMaterialTextures[DiffuseIndex].SampleLevel(sSceneSampler, texCoord, 0).rgb;
    payload.output = color * saturate(NoL);
}

[shader("miss")]
void Miss(inout RayPayload payload : SV_RayPayload)
{
    payload.output = 0;
}

[shader("raygeneration")]
void RayGen()
{
    RayPayload payload = (RayPayload)0;

    float2 dimInv = rcp(DispatchRaysDimensions().xy);
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint launchIndex1d = launchIndex.x + launchIndex.y * DispatchRaysDimensions().x;
    float2 texCoord = (float2)launchIndex * dimInv;

    float3 world = WorldFromDepth(texCoord, tDepth.SampleLevel(sSceneSampler, texCoord, 0).r, cViewProjectionInverse);
    float2 texCoord1 = texCoord + float2(dimInv.x, 0);
    float2 texCoord2 = texCoord + float2(0, -dimInv.y);
    float3 p1 = WorldFromDepth(texCoord1, tDepth.SampleLevel(sSceneSampler, texCoord1, 0).r, cViewProjectionInverse);
    float3 p2 = WorldFromDepth(texCoord2, tDepth.SampleLevel(sSceneSampler, texCoord2, 0).r, cViewProjectionInverse);
    float3 N = normalize(cross(p2 - world, p1 - world));

    float3 V = normalize(world - cViewInverse[3].xyz);
    float3 R = reflect(V, N);

    RayDesc ray;
    ray.Origin = world + 0.001f * R;
    ray.Direction = R;
    ray.TMin = 0.0f;
    ray.TMax = 10000;

    TraceRay(
        SceneBVH, 
        RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_FORCE_OPAQUE,
        0xFF,
        0,
        1,
        0,
        ray,
        payload
    );

    gOutput[launchIndex] = float4(payload.output, 1);
}