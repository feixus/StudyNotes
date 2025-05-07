cbuffer PerObjectData : register(b0) // b-const buffer t-texture s-sampler
{
    float4x4 cWorld;
    float4x4 cWorldViewProjection;
}

cbuffer PerFrameData : register(b1)
{
    float4x4 cLightViewProjection;
    float4x4 cViewInverse;
}

struct Light
{
    int Enabled;
	float3 Position;
	float3 Direction;
	float Intensity;
	float4 Color;
	float Range;
	float SpotLightAngle;
	float Attenuation;
	uint Type;
};

cbuffer LightData : register(b2)
{
    Light cLights[6];
}

Texture2D myDiffuseTexture : register(t0);
SamplerState myDiffuseSampler : register(s0);

Texture2D myNormalTexture : register(t1);
SamplerState myNormalSampler : register(s1);

Texture2D mySpecularTexture : register(t2);

Texture2D myShadowMapTexture : register(t3);
SamplerComparisonState myShadowMapSampler : register(s2);

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
    float2 texCoord : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : TEXCOORD1;
    float4 lpos : TEXCOORD2;
    float4 wpos : TEXCOORD3;
};

struct LightResult
{
    float4 Diffuse;
    float4 Specular;
};

float4 GetSpecularBlinnPhong(float3 viewDirection, float3 normal, float3 lightVector, float shininess)
{
    float3 halfway = normalize(lightVector - viewDirection);
    float4 specularStrength = dot(halfway, normal);
    return pow(saturate(specularStrength), shininess);
}

float4 GetSpecularPhong(float3 viewDirection, float3 normal, float3 lightVector, float shininess)
{
    float3 reflectedLight = reflect(-lightVector, normal);
    float4 specularStrength = dot(reflectedLight, -viewDirection);
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
    result.Specular = DoSpecular(light, normal, -light.Direction, viewDirection);
    return result;
}

LightResult DoLight(float3 worldPosition, float3 normal, float3 viewDirection, float3 shadowFactor)
{
    LightResult totalResult = (LightResult)0;

    for (int i = 1; i < 5; i++)
    {
        if (cLights[i].Enabled == 0)
        {
            continue;
        }

        if (cLights[i].Type != 0 && distance(worldPosition, cLights[i].Position) > cLights[i].Range)
        {
            continue;
        }

        LightResult result;

        switch(cLights[i].Type)
        {
        case 0:
        {
            result = DoDirectionalLight(cLights[i], normal, viewDirection);
        }
        break;
        case 1:
        {
            result = DoPointLight(cLights[i], worldPosition, normal, viewDirection);
        }
        break;
        }
        
        totalResult.Diffuse += result.Diffuse;
        totalResult.Specular += result.Specular;
    }

    return totalResult;
}

float3 CalculateNormal(float3 normal, float3 tangent, float3 bitangent, float2 texCoord, bool invertY)
{
    float3x3 normalMatrix = float3x3(tangent, bitangent, normal);
    float3 sampleNormal = myNormalTexture.Sample(myNormalSampler, texCoord).rgb;
    sampleNormal.xy = sampleNormal.xy * 2.0f - 1.0f;
    if (invertY)
    {
        sampleNormal.y = -sampleNormal.y;
    }
    sampleNormal = normalize(sampleNormal);
    return mul(sampleNormal, normalMatrix);
}

PSInput VSMain(VSInput input)
{
    PSInput result;
    
    result.position = mul(float4(input.position, 1.0f), cWorldViewProjection);
    result.texCoord = input.texCoord;
    result.normal = normalize(mul(input.normal, (float3x3)cWorld));
    result.tangent = normalize(mul(input.tangent, (float3x3)cWorld));
    result.bitangent = normalize(mul(input.bitangent, (float3x3)cWorld));
    result.lpos = mul(float4(input.position, 1.0f), mul(cWorld, cLightViewProjection));
    result.wpos = mul(float4(input.position, 1.0f), cWorld);
    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 diffuseSample = myDiffuseTexture.Sample(myDiffuseSampler, input.texCoord);
    if (diffuseSample.a <= 0.01f)
    {
        discard;
    }
   
    float3 viewDirection = normalize(input.wpos.xyz - cViewInverse[3].xyz);
    float3 normal = CalculateNormal(normalize(input.normal), normalize(input.tangent), normalize(input.bitangent), input.texCoord, true);
    
    // clip space via perspective divide to ndc space(positive Y is up), then to texture space(positive Y is down)
    input.lpos.xyz /= input.lpos.w;
    input.lpos.x = input.lpos.x / 2.0f + 0.5f;
    input.lpos.y = input.lpos.y / -2.0f + 0.5f;
    input.lpos.z -= 0.001f;
    
    int width, height;
    myShadowMapTexture.GetDimensions(width, height);
    float dx = 1.0f / width;
    float dy = 1.0f / height;
    
    float shadowFactor = 0;
    int kernelSize = 3;
    int hKernel = (kernelSize - 1) / 2;
    for (int x = -hKernel; x <= hKernel; x++)
    {
        for (int y = -hKernel; y <= hKernel; y++)
        {
            shadowFactor += myShadowMapTexture.SampleCmpLevelZero(myShadowMapSampler, input.lpos.xy + float2(dx * x, dy * y), input.lpos.z);
        }
    }
   
    shadowFactor /= kernelSize * kernelSize;

    LightResult mainLight = DoDirectionalLight(cLights[0], input.normal, viewDirection);
    mainLight.Diffuse *= shadowFactor;
    if (shadowFactor == 0)
    {
        mainLight.Specular *= 0.0f;
    }

    LightResult lightResults = DoLight(input.wpos.xyz, input.normal, viewDirection, shadowFactor);
    lightResults.Diffuse += mainLight.Diffuse;
    lightResults.Specular += mainLight.Specular;

    float4 specularSample = mySpecularTexture.Sample(myDiffuseSampler, input.texCoord);
    lightResults.Specular *= specularSample;
    lightResults.Diffuse *= diffuseSample;

    return saturate(lightResults.Diffuse + lightResults.Specular);
}