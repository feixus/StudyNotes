#include "Common.hlsli"
#include "ShadingModels.hlsli"

cbuffer LightData : register(b2)
{
    float4x4 cLightViewProjection[MAX_SHADOW_CASTERS];
    float4 cShadowMapOffsets[MAX_SHADOW_CASTERS];
}

#ifdef SHADOW
Texture2D tShadowMapTexture : register(t3);
SamplerComparisonState sShadowMapSampler : register(s1);

float DoShadow(float3 wPos, int shadowMapIndex)
{
    // clip space via perspective divide to ndc space(positive Y is up), then to texture space(positive Y is down)
    float4 lightPos = mul(float4(wPos, 1), cLightViewProjection[shadowMapIndex]);
    lightPos.xyz /= lightPos.w;
    lightPos.x = lightPos.x / 2.0f + 0.5f;
    lightPos.y = lightPos.y / -2.0f + 0.5f;
    lightPos.z += 0.0001f;
    
    float shadowFactor = 0;
    int hKernel = (PCF_KERNEL_SIZE - 1) / 2;
        
    float2 shadowMapStart = cShadowMapOffsets[shadowMapIndex].xy;
    float normalizeShadowMapSize = cShadowMapOffsets[shadowMapIndex].z;
        
    for (int x = -hKernel; x <= hKernel; x++)
    {
        for (int y = -hKernel; y <= hKernel; y++)
        {
            float2 texCoord = shadowMapStart + lightPos.xy * normalizeShadowMapSize + float2(SHADOWMAP_DX * x, SHADOWMAP_DX * y);
            shadowFactor += tShadowMapTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord, lightPos.z);
        }
    }

    return shadowFactor / (PCF_KERNEL_SIZE * PCF_KERNEL_SIZE);
}

#endif

float DoAttenuation(Light light, float distance)
{
    //smoothstep: cubic hermite polynomial
    return 1.0f - smoothstep(light.Range * light.Attenuation, light.Range, distance);
}

float GetAttenuation(Light light, float3 wPos)
{
    float attenuation = 1.0f;
    if (light.Type >= LIGHT_POINT)
    {
        float3 L = light.Position - wPos;
        float d = length(L);
        L = L / d;
        attenuation *= DoAttenuation(light, d);

        if (light.Type >= LIGHT_SPOT)
        {
            float minCos = cos(radians(light.SpotLightAngle * 0.5f));
            float maxCos = lerp(minCos, 1.0f, 1 - light.Attenuation);
            float cosAngle = dot(-L, light.Direction);
            float spotIntensity = smoothstep(minCos, maxCos, cosAngle);

            attenuation *= spotIntensity;
        }
    }
    return attenuation;
}

LightResult DoLight(Light light, float3 specularColor, float3 diffuseColor, float roughness, float3 wPos, float3 N, float3 V)
{
    float attenuation = GetAttenuation(light, wPos);
    float3 L = normalize(light.Position - wPos);
    LightResult result = DefaultLitBxDF(specularColor, roughness, diffuseColor, N, V, L, attenuation);

#ifdef SHADOW
    if (light.ShadowIndex >= 0)
    {
        float shadowFactor = DoShadow(wPos, light.ShadowIndex);
        result.Diffuse *= shadowFactor;
        result.Specular *= shadowFactor;
    }
#endif

    return result;
}