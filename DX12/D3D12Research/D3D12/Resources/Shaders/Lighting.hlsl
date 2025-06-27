#ifndef H_LIGHTING
#define H_LIGHTING

#include "Common.hlsl"

cbuffer LightData : register(b2)
{
    float4x4 cLightViewProjection[MAX_SHADOW_CASTERS];
    float4 cShadowMapOffsets[MAX_SHADOW_CASTERS];
}

struct LightResult
{
    float4 Diffuse;
    float4 Specular;
};

float GetSpecularBlinnPhong(float3 viewDirection, float3 normal, float3 lightVector, float shininess)
{
    float3 halfway = normalize(lightVector - viewDirection);
    float specularStrength = dot(halfway, normal);
    return pow(saturate(specularStrength), shininess);
}

float GetSpecularPhong(float3 viewDirection, float3 normal, float3 lightVector, float shininess)
{
    float3 reflectedLight = reflect(-lightVector, normal);
    float specularStrength = dot(reflectedLight, -viewDirection);
    return pow(saturate(specularStrength), shininess);
}

float4 DoDiffuse(Light light, float3 normal, float3 lightVector)
{
    return float4(light.Color.rgb * max(dot(normal, lightVector), 0), 1);
}

float4 DoSpecular(Light light, float3 normal, float3 lightVector, float3 viewDirection)
{
    return float4(light.Color.rgb * GetSpecularBlinnPhong(viewDirection, normal, lightVector, 15.0f), 1);
}

#ifdef SHADOW
Texture2D tShadowMapTexture : register(t3);
SamplerComparisonState sShadowMapSampler : register(s2);

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
            float minCos = cos(radians(light.SpotLightAngle));
            float maxCos = lerp(minCos, 1.0f, 1 - light.Attenuation);
            float cosAngle = dot(-L, light.Direction);
            float spotIntensity = smoothstep(minCos, maxCos, cosAngle);

            attenuation *= spotIntensity;
        }
    }
    return attenuation;
}

LightResult DoLight(Light light, float3 wPos, float3 normal, float3 viewDirection)
{
    LightResult result;
    float3 L = normalize(light.Position - wPos);
    float attenuation = GetAttenuation(light, wPos);
    result.Diffuse = light.Color.w * attenuation * DoDiffuse(light, normal, L);
    result.Specular = light.Color.w * attenuation * DoSpecular(light, normal, L, viewDirection);

#ifdef SHADOW
    if (light.ShadowIndex != -1)
    {
        float3 vLight = normalize(wPos - light.Position);
        float faceIndex = GetCubeFaceIndex(vLight);

        float shadowFactor = DoShadow(wPos, light.ShadowIndex);
        result.Diffuse *= shadowFactor;
        result.Specular *= shadowFactor;
    }
#endif

    return result;
}

#endif