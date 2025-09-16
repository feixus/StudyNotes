#include "Common.hlsli"
#include "ShadingModels.hlsli"
#include "CommonBindings.hlsli"

#define MAX_SHADOW_CASTERS 32

cbuffer LightData : register(b2)
{
    float4x4 cLightViewProjection[MAX_SHADOW_CASTERS];
    float4 cCascadeDepths;
    uint cNumCascades;
}

float3 TangentSpaceNormalMapping(float3 sampledNormal, float3x3 TBN, float2 tex, bool invertY)
{
    sampledNormal.xy = sampledNormal.xy * 2.0f - 1.0f;

#ifdef NORMAL_BC5
    sampledNormal.z = sqrt(saturate(1.0f - dot(sampledNormal.xy, sampledNormal.xy)));
#endif

    if (invertY)
    {
        sampledNormal.y = -sampledNormal.y;
    }

    sampledNormal = normalize(sampledNormal);
    return mul(sampledNormal, TBN);
}

float DoShadow(float3 wPos, int shadowMapIndex, float invShadowSize)
{
    // clip space via perspective divide to ndc space(positive Y is up), then to texture space(positive Y is down)
    float4 lightPos = mul(float4(wPos, 1), cLightViewProjection[shadowMapIndex]);
    lightPos.xyz /= lightPos.w;
    lightPos.x = lightPos.x / 2.0f + 0.5f;
    lightPos.y = lightPos.y / -2.0f + 0.5f;
    lightPos.z += 0.0001f;
        
    float2 texCoord = lightPos.xy;

    Texture2D shadowTexture = tShadowMapTextures[shadowMapIndex];

    const float Dilation = 2.0f;
    float d1 = Dilation * invShadowSize * 0.125f;
    float d2 = Dilation * invShadowSize * 0.875f;
    float d3 = Dilation * invShadowSize * 0.625f;
    float d4 = Dilation * invShadowSize * 0.375f;
    float result = (
        2.0f * shadowTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord, lightPos.z) +
        shadowTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2(-d2,  d1), lightPos.z) +
        shadowTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2(-d1, -d2), lightPos.z) +
        shadowTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2( d2, -d1), lightPos.z) +
        shadowTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2( d1,  d2), lightPos.z) +
        shadowTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2(-d4,  d3), lightPos.z) +
        shadowTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2(-d3, -d4), lightPos.z) +
        shadowTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2( d4, -d3), lightPos.z) +
        shadowTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2( d3,  d4), lightPos.z)
    ) / 10.0f;

    return result * result;
}

// Angle >= Umbra -> 0
// Angle < Penumbra -> 1
// gradient between Umbra and Penumbra
float DirectionalAttenuation(float3 L, float3 direction, float cosUmbra, float cosPenumbra)
{
    float cosAngle = dot(-normalize(L), direction);
    float falloff = saturate((cosAngle - cosUmbra) / (cosPenumbra - cosUmbra));
    return falloff * falloff;
}

// distance between rays is proportional to distance squared
// extra windowing function to make light radius finite
//https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
float RadialAttenuation(float3 L, float range)
{
    float distSq = dot(L, L);
    float distanceAttenuation = 1 / (distSq + 1);
    float windowing = Square(saturate(1 - Square(distSq * Square(rcp(range)))));
    return distanceAttenuation * windowing;
}

float GetAttenuation(Light light, float3 wPos)
{
    float attenuation = 1.0f;
    if (light.Type >= LIGHT_POINT)
    {
        float3 L = light.Position - wPos;
        attenuation *= RadialAttenuation(L, light.Range);

        if (light.Type >= LIGHT_SPOT)
        {
            attenuation *= DirectionalAttenuation(L, light.Direction, light.SpotlightAngles.y, light.SpotlightAngles.x);
        }
    }
    return attenuation;
}

float3 ApplyAmbientLight(float3 diffuse, float ao, float3 lightColor)
{
    return ao * diffuse * lightColor;
}

uint GetShadowIndex(Light light, float3 pos, float3 wPos, float3 vPos)
{
    int shadowIndex = light.ShadowIndex;
    if (light.Type == LIGHT_DIRECTIONAL)
    {
        float4 splits = vPos.z > cCascadeDepths;
        float4 cascades = cCascadeDepths > 0;
        int cascadeIndex = dot(splits, cascades);

#define FADE_SHADOW_CASCADES 1
#define FADE_THRESHOLD 0.1f
#if FADE_SHADOW_CASCADES
        float nextSplit = cCascadeDepths[cascadeIndex];
        float splitRange = cascadeIndex == 0 ? nextSplit : nextSplit - cCascadeDepths[cascadeIndex - 1];
        float fadeFactor = (nextSplit - vPos.z) / splitRange;
        if (fadeFactor < FADE_THRESHOLD && cascadeIndex != cNumCascades - 1)
        {
            float lerpAmount = smoothstep(0.0f, FADE_THRESHOLD, fadeFactor);
            float dither = InterleavedGradientNoise(pos.xy);
            if (lerpAmount < dither)
            {
                cascadeIndex++;
            }
        }
#endif
        shadowIndex += cascadeIndex;
    }
    else if (light.Type == LIGHT_POINT)
    {
        shadowIndex += GetCubeFaceIndex(wPos - light.Position);
    }
    return shadowIndex;
}

LightResult DoLight(Light light, float3 specularColor, float3 diffuseColor, float roughness, float4 pos, float3 wPos, float3 vPos, float3 N, float3 V)
{
    LightResult result = (LightResult)0;

    float attenuation = GetAttenuation(light, wPos);
    if (attenuation <= 0)
    {
        return result;
    }

    float visibility = 1.0f;
    if (light.ShadowIndex >= 0)
    {

#define INLINE_RT_SHADOWS 0
#if INLINE_RT_SHADOWS
        RayDesc ray;
        ray.Origin = wPos + N * 0.01f;
        ray.Direction = light.Position - wPos;
        ray.TMin = 0.01f;
        ray.TMax = 1;

        RayQuery<RAY_FLAG_NONE> q;
        q.TraceRayInline(tAccelerationStructure, RAY_FLAG_NONE, ~0, ray);
        q.Proceed();

        if (q.CommitedStatus == COMMITTED_TRIANGLE_HIT)
        {
            visibility = 0;
        }
#else
        int shadowIndex = GetShadowIndex(light, pos.xyz, wPos, vPos);

#define VISUALIZE_CASCADES 0
#if VISUALIZE_CASCADES
        if (light.Type == LIGHT_DIRECTIONAL)
        {
            static float4 COLORS[4] = {
                float4(1, 0, 0, 1),
                float4(0, 1, 0, 1),
                float4(0, 0, 1, 1),
                float4(1, 1, 0, 1)
            };
            result.Diffuse += 0.4f * COLORS[shadowIndex - light.ShadowIndex].xyz;
            result.Specular = 0;
            return result;
        }
#endif
     
        visibility = DoShadow(wPos, shadowIndex, light.InvShadowSize);
        if (visibility <= 0)
        {
            return result;
        }
#endif
    }

    float3 L = normalize(light.Position - wPos);
    result = DefaultLitBxDF(specularColor, roughness, diffuseColor, N, V, L, attenuation);

    float4 color = light.GetColor();
    result.Diffuse *= color.rgb * light.Intensity * visibility;
    result.Specular *= color.rgb * light.Intensity * visibility;

    return result;
}

// volumetric scattering - Henyey-Greenstein phase function
#define G_SCATTERING 0.0001f
float ComputeScattering(float LoV)
{
    float result = 1.0f - G_SCATTERING * G_SCATTERING;
    result /= (4.0f * PI * pow(1.0f + G_SCATTERING * G_SCATTERING - (2.0f * G_SCATTERING) * LoV, 1.5f));
    return result;
}

float3 ApplyVolumetricLighting(float3 cameraPos, float3 worldPos, float3 pos, float4x4 view, Light light, int samples)
{
    const float fogValue = 0.3f;
    float3 rayVector = cameraPos - worldPos;
    float3 rayStep = rayVector / samples;
    float3 accumFog = 0.0f.xxx;
    float3 currentPosition = worldPos;

    float ditherValue = InterleavedGradientNoise(pos.xy);
    currentPosition += rayStep * ditherValue;

    for (int i = 0; i < samples; i++)
    {
        float4 vPos = mul(float4(currentPosition, 1.0f), view);
        float visibility = 1.0f;
        if (light.ShadowIndex >= 0)
        {
            int shadowMapIndex = GetShadowIndex(light, pos, currentPosition, vPos.xyz);
            float4x4 lightViewProjection = cLightViewProjection[shadowMapIndex];
            float4 lightPos = mul(float4(currentPosition, 1.0f), lightViewProjection);
            lightPos.xyz /= lightPos.w;
            lightPos.xy = lightPos.xy * float2(0.5f, -0.5f) + 0.5f;
            visibility = tShadowMapTextures[shadowMapIndex].SampleCmpLevelZero(sShadowMapSampler, lightPos.xy, lightPos.z);
        }

        accumFog += visibility * fogValue * ComputeScattering(dot(rayVector, light.Direction)).xxx * light.GetColor().rgb * light.Intensity;
        currentPosition += rayStep;
    }
    accumFog /= samples;
    return accumFog;
}