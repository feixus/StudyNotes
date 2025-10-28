#include "Common.hlsli"
#include "ShadingModels.hlsli"
#include "Lighting.hlsli"
#include "SkyCommon.hlsli"
#include "RaytracingCommon.hlsli"

#define RAY_CONE_TEXTURE_LOD 1
#define SECONDARY_SHADOW_RAY 1

GlobalRootSignature GlobalRootSig = 
{
    "CBV(b0, visibility=SHADER_VISIBILITY_ALL),"
    "DescriptorTable(UAV(u0, numDescriptors=1), visibility=SHADER_VISIBILITY_ALL),"
    "DescriptorTable(SRV(t5, numDescriptors=6), visibility=SHADER_VISIBILITY_ALL),"
    "DescriptorTable(SRV(t500, numDescriptors=1), visibility=SHADER_VISIBILITY_ALL),"
    "DescriptorTable(SRV(t1000, numDescriptors=128, space = 2), visibility=SHADER_VISIBILITY_ALL),"
    "staticSampler(s0, filter=FILTER_MIN_MAG_LINEAR_MIP_POINT, visibility=SHADER_VISIBILITY_ALL)"
};

RWTexture2D<float4> uOutput : register(u0);

struct ViewData
{
    float4x4 ViewInverse;
    float4x4 ProjectionInverse;
    uint NumLights;
    float ViewPixelSpreadAngle;
};
ConstantBuffer<ViewData> cViewData : register(b0);

struct HitData
{
    int DiffuseIndex;
    int NormalIndex;
    int RoughnessIndex;
    int MetallicIndex;
    uint VertexBufferOffset;
    uint IndexBufferOffset;
};
ConstantBuffer<HitData> cHitData : register(b1);

struct Vertex
{
    float3 position;
    float2 texCoord;
    float3 normal;
    float3 tangent;
    float3 bitangent;
};

struct RayPayload
{
    float3 output;
    RayCone rayCone;
};

struct ShadowRayPayload
{
    uint hit;
};

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attrib)
{
    payload.rayCone = PropagateRayCone(payload.rayCone, 0, RayTCurrent());

    // resolve geometry data
    uint3 indices = tGeometryData.Load3(cHitData.IndexBufferOffset + PrimitiveIndex() * sizeof(uint3));
    float3 b = float3(1.0f - attrib.barycentrics.x - attrib.barycentrics.y, attrib.barycentrics.x, attrib.barycentrics.y);
    Vertex v0 = tGeometryData.Load<Vertex>(cHitData.VertexBufferOffset + indices.x * sizeof(Vertex));
    Vertex v1 = tGeometryData.Load<Vertex>(cHitData.VertexBufferOffset + indices.y * sizeof(Vertex));
    Vertex v2 = tGeometryData.Load<Vertex>(cHitData.VertexBufferOffset + indices.z * sizeof(Vertex));
    float2 texCoord = v0.texCoord * b.x + v1.texCoord * b.y + v2.texCoord * b.z;

    float3 N = v0.normal * b.x + v1.normal * b.y + v2.normal * b.z;
    float3 T = v0.tangent * b.x + v1.tangent * b.y + v2.tangent * b.z;
    float3 B = v0.bitangent * b.x + v1.bitangent * b.y + v2.bitangent * b.z;
    float3x3 TBN = float3x3(T, B, N);

#if RAY_CONE_TEXTURE_LOD
    float3 positions[3] = {v0.position, v1.position, v2.position};
    float2 texCoords[3] = {v0.texCoord, v1.texCoord, v2.texCoord};
    float2 textureDimensions;
    tMaterialTextures[cHitData.DiffuseIndex].GetDimensions(textureDimensions.x, textureDimensions.y);
    float mipLevel = ComputeRayConeTextureLOD(payload.rayCone, positions, texCoords, textureDimensions);
else
    float mipLevel = 2.0f;
#endif

    // get material data
    float3 diffuse = tMaterialTextures[cHitData.DiffuseIndex].SampleLevel(sDiffuseSampler, texCoord, mipLevel).rgb;
    float3 sampledNormal = tMaterialTextures[cHitData.NormalIndex].SampleLevel(sDiffuseSampler, texCoord, mipLevel).rgb;
    float metalness = tMaterialTextures[cHitData.MetallicIndex].SampleLevel(sDiffuseSampler, texCoord, mipLevel).r;
    float roughness = 0.5f;
    float specular = 0.5f;

    float3 specularColor = ComputeF0(specular, diffuse, metalness);
    float3 wPos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    float3 V = normalize(-WorldRayDirection());
    N = TangentSpaceNormalMapping(sampledNormal, TBN, false);

    LightResult totalResult = (LightResult)0;
    for (int i = 0; i < cViewData.NumLights; ++i)
    {
        Light light = tLights[i];
        float attenuation = GetAttenuation(light, wPos);
        if (attenuation <= 0.0f)
        {
            continue;
        }

        float3 L = normalize(light.Position - wPos);
        if (light.Type == LIGHT_DIRECTIONAL)
        {
            L = -light.Direction;
        }

    #if SECONDARY_SHADOW_RAY
        RayDesc ray;
        ray.Origin = wPos + 0.001f * L;
        ray.Direction = L;
        ray.TMin = 0.0f;
        ray.TMax = length(wPos - light.Position);

        ShadowRayPayload payload = (ShadowRayPayload)0;
        // trace the ray
        TraceRay(
            tAccelerationStructure,                                         // Acceleration structure
            RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_FORCE_OPAQUE,    // Ray flags
            0xFF,                                                           // InstanceInclusionMask
            1,                                                              // RayContributionToHitGroupIndex
            2,                                                              // MultiplierForGeometryContributionToHitGroupIndex
            1,                                                              // MissShaderIndex
            ray,                                                            // Ray
            payload                                                         // Payload
        );
        attenuation *= payload.hit;
    #endif

        LightResult result = DefaultLitBxDF(specularColor, roughness, diffuse, N, V, L, attenuation);
        float4 color = light.GetColor();
        totalResult.Diffuse += result.Diffuse * color.rgb * light.Intensity;
        totalResult.Specular += result.Specular * color.rgb * light.Intensity;
    }

    payload.output = totalResult.Diffuse + totalResult.Specular + ApplyAmbientLight(diffuse, 1.0f, 0.1f);
}

[shader("closesthit")]
void ShadowClosestHit(inout ShadowRayPayload payload, BuiltInTriangleIntersectionAttributes attrib)
{
    payload.hit = 0;
}

[shader("miss")]
void ShadowMiss(inout ShadowRayPayload payload : SV_RayPayload)
{
    payload.hit = CIESky(WorldRayDirection(), -tLights[0].Direction);
}

[shader("miss")]
void Miss(inout RayPayload payload : SV_RayPayload)
{
    payload.output = 0;
}

[shader("raygeneration")]
void RayGen()
{
    float2 dimInv = rcp(DispatchRaysDimensions().xy);
    uint2 launchIndex = DispatchRaysIndex().xy;
    float2 texCoord = (float2)launchIndex * dimInv;

    float depth = tDepth.SampleLevel(sDiffuseSampler, texCoord, 0).r;
    float3 view = ViewFromDepth(texCoord, depth, cViewData.ProjectionInverse);
    float3 world = WorldFromDepth(texCoord, tDepth.SampleLevel(sDiffuseSampler, texCoord, 0).r, cViewData.ViewProjectionInverse);
    
    RayCone cone;
    cone.Width = 0;
    cone.SpreadAngle = cViewData.ViewPixelSpreadAngle;

    RayPayload payload;
    payload.rayCone = PropagateRayCone(cone, 0.0f, depth);
    payload.output = 0;

    float4 reflectionSample = tSceneNormals.SampleLevel(sDiffuseSampler, texCoord, 0);
    float3 N = reflectionSample.rgb;
    float reflectivity = reflectionSample.a;
    if (reflectivity > 0)
    {
        float3 V = normalize(world - cViewData.ViewInverse[3].xyz);
        float3 R = reflect(V, N);

        RayDesc ray;
        ray.Origin = world + 0.001f * R;
        ray.Direction = R;
        ray.TMin = 0.0f;
        ray.TMax = 10000;

        TraceRay(
            tAccelerationStructure, 
            RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_FORCE_OPAQUE,
            0xFF,
            0,
            2,
            0,
            ray,
            payload
        );
    }

    float4 colorSample = tPreviousSceneColor.SampleLevel(sDiffuseSampler, texCoord, 0);
    uOutput[launchIndex] = colorSample + float4(reflectivity * payload.output, 0);
}