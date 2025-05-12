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
    return light.Color * max(dot(normal, lightVector), 0);
}

float4 DoSpecular(Light light, float3 normal, float3 lightVector, float3 viewDirection)
{
    return light.Color * GetSpecularBlinnPhong(viewDirection, normal, lightVector, 15.0f);
}

float DoAttenuation(Light light, float distance)
{
    return 1.0f - smoothstep(light.Range * light.Attenuation, light.Range, distance);
}

LightResult DoPointLight(Light light, float3 worldPosition, float3 normal, float3 viewDirection)
{
    LightResult result;
    float3 L = light.Position - worldPosition;
    float distance = length(L);
    L = L / distance;

    float attenuation = DoAttenuation(light, distance);
    result.Diffuse = DoDiffuse(light, normal, L) * attenuation;
    result.Specular = DoSpecular(light, normal, L, viewDirection) * attenuation;
    return result;
}

LightResult DoDirectionalLight(Light light, float3 normal, float3 viewDirection)
{
    LightResult result;
    result.Diffuse = light.Intensity * DoDiffuse(light, normal, -light.Direction);
    result.Specular = light.Intensity * DoSpecular(light, normal, -light.Direction, viewDirection);
    return result;
}

LightResult DoSpotLight(Light light, float3 worldPosition, float3 normal, float3 viewDirection)
{
    LightResult result = (LightResult)0;
    float3 L = light.Position - worldPosition;
    float distance = length(L);
    L = L / distance;

    float minCos = cos(radians(light.SpotLightAngle));
    float maxCos = lerp(minCos, 1.0f, 1 - light.Attenuation);
    float cosAngle = dot(-L, light.Direction);
    float spotIntensity = smoothstep(minCos, maxCos, cosAngle);

    float attenuation = DoAttenuation(light, distance);

    result.Diffuse = light.Intensity * attenuation * spotIntensity * DoDiffuse(light, normal, L);
    result.Specular = light.Intensity * attenuation * spotIntensity * DoSpecular(light, normal, L, viewDirection);
    return result;
}