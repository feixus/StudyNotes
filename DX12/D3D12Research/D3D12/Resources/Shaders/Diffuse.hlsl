#include "Common.hlsli"
#include "Lighting.hlsli"

#define BLOCK_SIZE 16

#define RootSig "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                "CBV(b0, visibility = SHADER_VISIBILITY_ALL), " \
                "CBV(b1, visibility = SHADER_VISIBILITY_ALL), " \
                "CBV(b2, visibility = SHADER_VISIBILITY_PIXEL), " \
                "DescriptorTable(SRV(t1000, numDescriptors = 128, space = 2), visibility = SHADER_VISIBILITY_PIXEL), " \
                "DescriptorTable(SRV(t3, numDescriptors = 7), visibility = SHADER_VISIBILITY_PIXEL), " \
                "DescriptorTable(SRV(t10, numDescriptors = 32, space = 1), visibility = SHADER_VISIBILITY_PIXEL), " \
                "SRV(t500, visibility = SHADER_VISIBILITY_PIXEL), " \
                "StaticSampler(s0, filter = FILTER_ANISOTROPIC, maxAnisotropy = 4, visibility = SHADER_VISIBILITY_PIXEL), " \
                "StaticSampler(s1, filter = FILTER_MIN_MAG_MIP_POINT, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, visibility = SHADER_VISIBILITY_PIXEL), " \
                "StaticSampler(s2, filter = FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, visibility = SHADER_VISIBILITY_PIXEL, comparisonFunc = COMPARISON_GREATER)"

struct PerObjectData
{
    float4x4 World;
    int Diffuse;
    int Normal;
    int Roughness;
    int Metallic;
};

struct PerViewData
{
    float4x4 View;
    float4x4 Projection;
    float4x4 ViewProjection;
    float4x4 ReprojectionMatrix;
    float4 ViewPosition;
    float2 InvScreenDimensions;
    float NearZ;
    float FarZ;
    int FrameIndex;
    int SsrSamples;
    int2 padd;
#if CLUSTERED_FORWARD
    int4 ClusterDimensions;
    int2 ClusterSize;
    float2 LightGridParams;
#endif
};

ConstantBuffer<PerObjectData> cObjectData : register(b0);
ConstantBuffer<PerViewData> cViewData : register(b1);

struct VSInput
{
    float3 position : POSITION;
    float2 texCoord : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : TEXCOORD1;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 positionWS : POSITION_WS;
    float3 positionVS : POSITION_VS;
    float2 texCoord : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : TEXCOORD1;
};

#if TILED_FORWARD
Texture2D<uint2> tLightGrid : register(t3);
#elif CLUSTERED_FORWARD
StructuredBuffer<uint2> tLightGrid : register(t3);
#endif

StructuredBuffer<uint> tLightIndexList : register(t4);

#if CLUSTERED_FORWARD
uint GetSliceFromDepth(float depth)
{
    return floor(log(depth) * cViewData.LightGridParams.x - cViewData.LightGridParams.y);
}
#endif

LightResult DoLight(float4 pos, float3 wPos, float3 N, float3 V, float3 diffuseColor, float3 specularColor, float roughness)
{
#if TILED_FORWARD
    uint2 tileIndex = uint2(floor(pos.xy / BLOCK_SIZE));
    uint startOffset = tLightGrid[tileIndex].x;
    uint lightCount = tLightGrid[tileIndex].y;
#elif CLUSTERED_FORWARD
    uint3 clusterIndex3D = uint3(floor(pos.xy / cViewData.ClusterSize), GetSliceFromDepth(pos.w));
    uint clusterIndex1D = clusterIndex3D.x + cViewData.ClusterDimensions.x * (clusterIndex3D.y + clusterIndex3D.z * cViewData.ClusterDimensions.y);
    uint startOffset = tLightGrid[clusterIndex1D].x;
    uint lightCount = tLightGrid[clusterIndex1D].y; // lightCount = 0 means no light in this cluster
#endif

    LightResult totalResult = (LightResult)0;

    for (uint i = 0; i < lightCount; i++)
    {
        uint lightIndex = tLightIndexList[startOffset + i];
        Light light = tLights[lightIndex];

        LightResult result = DoLight(light, specularColor, diffuseColor, roughness, pos, wPos, N, V);
        totalResult.Diffuse += result.Diffuse;
        totalResult.Specular += result.Specular;
    }

    return totalResult;
}

float3 ScreenSpaceReflectionsRT(float3 positionWS, float4 position, float3 N, float3 V, float R)
{
    float3 ssr = 0;
#if _INLINE_RT
    const float roughnessThreshold = 0.1f;
    bool ssrEnabled = R < roughnessThreshold;
    if (ssrEnabled)
    {
        float3 reflectionWS = normalize(reflect(-V, N));
        const float reflectionThreshold = 0.0f;
        if (dot(V, reflectionWS) <= reflectionThreshold)
        {
            RayDesc ray;
            ray.Origin = positionWS;
            ray.Direction = reflectionWS;
            ray.TMin = 0.001f;
            ray.TMax = position.w;

            RayQuery<RAY_FLAG_NONE> q;
            q.TraceRayInline(
                tAccelerationStructure,
                RAY_FLAG_NONE,
                ~0,
                ray);
            q.Proceed();

            if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
            {
                float3 hitWS = ray.Origin + ray.Direction * q.CommittedRayT();
                float3 hitVS = mul(float4(hitWS, 1), cViewData.View).xyz;
                float3 texCoord = ViewToWindow(hitVS, cViewData.Projection);
                float sceneDepth = tDepth.SampleLevel(sClampSampler, texCoord.xy, 0).r;
                float viewDepth = texCoord.z;
                const float thickness = 0.05f;
                if (abs(sceneDepth - viewDepth) < thickness)
                {
                    ssr = saturate(0.5f * tPreviousSceneColor.SampleLevel(sClampSampler, texCoord.xy, 0).xyz);
                    float2 dist = (float2(texCoord.x, 1.0f - texCoord.y) * 2.0f) - float2(1.0f, 1.0f);
                    float edgeAttenuation = (1.0f - q.CommittedRayT() / position.w) * 4.0f;
                    edgeAttenuation = saturate(edgeAttenuation);
                    edgeAttenuation *= smoothstep(0.0f, 0.5f, saturate(1.0f - dot(dist, dist)));
                    ssr *= edgeAttenuation;
                }
            }
        }
    }
#endif
    return ssr;
}

float3 ScreenSpaceReflections(float4 position, float3 positionVS, float3 N, float3 V, float R)
{
    float3 ssr = 0;
    const float roughnessThreshold = 0.1f;
    bool ssrEnabled = R < roughnessThreshold;
    if (ssrEnabled)
    {
        float reflectionThreshold = 0.0f;
        float3 reflectionWs = normalize(reflect(-V, N));
        if (dot(V, reflectionWs) <= reflectionThreshold)
        {
            uint frameIndex = cViewData.FrameIndex;
            float jitter = InterleavedGradientNoise(position.xy, frameIndex) - 1.0f;

            uint maxSteps = cViewData.SsrSamples;

            float3 rayStartVS = positionVS;
            float linearDepth = positionVS.z;
            float3 reflectionVs = mul(reflectionWs, (float3x3)cViewData.View);
            float3 rayEndVs = rayStartVS + (reflectionVs * linearDepth);

            float3 rayStart = ViewToWindow(rayStartVS, cViewData.Projection);
            float3 rayEnd = ViewToWindow(rayEndVs, cViewData.Projection);

            float3 rayStep = ((rayEnd - rayStart) / float(maxSteps));
            rayStep = rayStep / length(rayEnd.xy - rayStart.xy);
            float3 rayPos = rayStart + (rayStep * jitter);
            float zThickness = abs(rayStep.z);

            uint hitIndex = 0;
            float3 bestHit = rayPos;
            float prevSceneZ = rayStart.z;
            for (uint currStep = 0; currStep < maxSteps; currStep += 4)
            {
                uint4 step = float4(1, 2, 3, 4) + currStep;
                float4 sceneZ = float4(
                    tDepth.SampleLevel(sClampSampler, rayPos.xy + rayStep.xy * step.x, 0).x,
                    tDepth.SampleLevel(sClampSampler, rayPos.xy + rayStep.xy * step.y, 0).x,
                    tDepth.SampleLevel(sClampSampler, rayPos.xy + rayStep.xy * step.z, 0).x,
                    tDepth.SampleLevel(sClampSampler, rayPos.xy + rayStep.xy * step.w, 0).x
                );
                float4 currentPosition = rayPos.z + rayStep.z * step;
                uint4 zTest = abs(sceneZ - currentPosition - zThickness) < zThickness;
                uint zMask = (zTest.x << 0) | (zTest.y << 1) | (zTest.y << 2) | (zTest.w << 3);
                if (zMask > 0)
                {
                    uint firstHit = firstbitlow(zMask);
                    if (firstHit > 0)
                    {
                        prevSceneZ = sceneZ[firstHit - 1];
                    }

                    bestHit = rayPos + (rayStep * float(currStep + firstHit + 1));
                    float zAfter = sceneZ[firstHit] - bestHit.z;
                    float zBefore = (prevSceneZ - bestHit.z) + rayStep.z;
                    float weight = saturate(zAfter / (zAfter - zBefore));
                    float3 prevRayPos = bestHit - rayStep;
                    bestHit = (prevRayPos * weight) + (bestHit * (1.0 - weight));
                    hitIndex = currStep + firstHit;
                    break;
                }
                prevSceneZ = sceneZ.w;
            }

            float4 hitColor = 0;
            if (hitIndex > 0)
            {
                float4 texCoord = float4(bestHit.xy, 0, 1);
                texCoord = mul(texCoord, cViewData.ReprojectionMatrix);
                float2 distanceFromCenter = float2(texCoord.x, 1.0f - texCoord.y) * 2.0f - float2(1.0f, 1.0f);
                float edgeAttenuation = saturate((1.0f - (float(hitIndex) / maxSteps)) * 4.0f);
                edgeAttenuation *= smoothstep(0.0f, 0.5f, saturate(1.0f - dot(distanceFromCenter, distanceFromCenter)));
                float3 reflectionResult = tPreviousSceneColor.SampleLevel(sClampSampler, texCoord.xy, 0).xyz;
                hitColor = float4(reflectionResult, edgeAttenuation);
            }

            float roughnessMask = saturate(1.0 - (R / (1 - roughnessThreshold)));
            float ssrWeight = hitColor.w * roughnessMask;
            ssr = saturate(hitColor.xyz * ssrWeight);
        }
    }
    return ssr;
}

[RootSignature(RootSig)]
PSInput VSMain(VSInput input)
{
    PSInput result;   
    result.positionWS = mul(float4(input.position, 1.0f), cObjectData.World).xyz;
    result.positionVS = mul(float4(result.positionWS, 1.0f), cViewData.View).xyz;
    result.position = mul(float4(result.positionWS, 1.0f), cViewData.ViewProjection);
    result.texCoord = input.texCoord;
    result.normal = normalize(mul(input.normal, (float3x3)cObjectData.World));
    result.tangent = normalize(mul(input.tangent, (float3x3)cObjectData.World));
    result.bitangent = normalize(mul(input.bitangent, (float3x3)cObjectData.World));
    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 baseColor = tMaterialTextures[cObjectData.Diffuse].Sample(sDiffuseSampler, input.texCoord);
    float3 sampledNormal = tMaterialTextures[cObjectData.Normal].Sample(sDiffuseSampler, input.texCoord).xyz;
    float metalness = tMaterialTextures[cObjectData.Metallic].Sample(sDiffuseSampler, input.texCoord).r;
    float r = 0.5f; //tMaterialTextures[cObjectData.Roughness].Sample(sDiffuseSampler, input.texCoord).x;
    float3 specular = 0.5f; 

    float3 diffuseColor = ComputeDiffuseColor(baseColor.rgb, metalness);
    float3 specularColor = ComputeF0(specular.r, baseColor.rgb, metalness);

    float3x3 TBN = float3x3(normalize(input.tangent), normalize(input.bitangent), normalize(input.normal));
    float3 N = TangentSpaceNormalMapping(sampledNormal, TBN, input.texCoord, true);
    float3 V = normalize(cViewData.ViewPosition.xyz - input.positionWS);
    
    float3 ssr = 0;
    if (cViewData.SsrSamples > 0)
    {
        if (cViewData.SsrSamples < 32)
        {
            ssr = ScreenSpaceReflections(input.position, input.positionVS, N, V, r);
        }
        else
        {
            ssr = ScreenSpaceReflectionsRT(input.positionWS, input.position, N, V, r);
        }
    }

    LightResult lightResults = DoLight(input.position, input.positionWS, N, V, diffuseColor, specularColor, r);

    float ao = tAO.Sample(sDiffuseSampler, (float2)input.position.xy * cViewData.InvScreenDimensions, 0).r; 
    float3 color = lightResults.Diffuse + lightResults.Specular + ssr * ao;
    color += ApplyAmbientLight(diffuseColor, ao, tLights[0].GetColor().rgb * 0.1f);
    color += 0.1f * ApplyVolumetricLighting(cViewData.ViewPosition.xyz, input.positionWS, input.position, cViewData.View, tLights[0], 10);

    return float4(color, baseColor.a);
}