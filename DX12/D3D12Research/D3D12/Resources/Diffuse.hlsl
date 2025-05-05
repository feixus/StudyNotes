cbuffer ObjectData : register(b0) // b-const buffer t-texture s-sampler
{
    float4x4 World;
    float4x4 WorldViewProjection;
}

cbuffer PerFrameData : register(b1)
{
    float4 LightPosition;
    float4x4 LightViewProjection;
    float4x4 ViewInverse;
}

Texture2D myDiffuseTexture : register(t0);
Texture2D myNormalTexture : register(t1);
Texture2D mySpecularTexture : register(t2);
SamplerState mySampler : register(s0);

Texture2D myShadowMap : register(t3);
SamplerComparisonState myShadowSampler : register(s1);

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

float4 GetSpecularBlinnPhong(float3 viewDirection, float3 normal, float2 texCoord, float3 lightVector, float shininess)
{
    float3 reflectedLight = reflect(-lightVector, normal);
    float specularStrength = dot(reflectedLight, -viewDirection);
    float4 sampledSpecular = mySpecularTexture.Sample(mySampler, texCoord);
    return sampledSpecular * pow(saturate(specularStrength), shininess);
}

float3 CalculateNormal(float3 normal, float3 tangent, float3 bitangent, float2 texCoord, bool invertY)
{
    float3x3 normalMatrix = float3x3(tangent, bitangent, normal);
    float3 sampleNormal = myNormalTexture.Sample(mySampler, texCoord).rgb;
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
    
    result.position = mul(float4(input.position, 1.0f), WorldViewProjection);
    result.texCoord = input.texCoord;
    result.normal = normalize(mul(input.normal, (float3x3)World));
    result.tangent = normalize(mul(input.tangent, (float3x3)World));
    result.bitangent = normalize(mul(input.bitangent, (float3x3) World));
    result.lpos = mul(float4(input.position, 1.0f), mul(World, LightViewProjection));
    result.wpos = mul(float4(input.position, 1.0f), World);
    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 textureColor = myDiffuseTexture.Sample(mySampler, input.texCoord);
    if (textureColor.a <= 0.01f)
    {
        discard;
    }
   
    float3 normal = CalculateNormal(normalize(input.normal), normalize(input.tangent), normalize(input.bitangent), input.texCoord, true);
    
    float3 lightDirection = normalize(LightPosition.xyz);
    float3 viewDirection = normalize(input.wpos.xyz - ViewInverse[3].xyz);
    float4 specular = GetSpecularBlinnPhong(viewDirection, normal, input.texCoord, lightDirection, 10.0f);
    
    float diffuse = saturate(dot(lightDirection, normal));
    
    // clip space via perspective divide to ndc space(positive Y is up), then to texture space(positive Y is down)
    input.lpos.xyz /= input.lpos.w;
    input.lpos.x = input.lpos.x / 2.0f + 0.5f;
    input.lpos.y = input.lpos.y / -2.0f + 0.5f;
    input.lpos.z -= 0.001f;
    
    int width, height;
    myShadowMap.GetDimensions(width, height);
    float dx = 1.0f / width;
    float dy = 1.0f / height;
    
    float shadowFactor = 0;
    int kernelSize = 3;
    int hKernel = (kernelSize - 1) / 2;
    for (int x = -hKernel; x <= hKernel; x++)
    {
        for (int y = -hKernel; y <= hKernel; y++)
        {
            shadowFactor += myShadowMap.SampleCmpLevelZero(myShadowSampler, input.lpos.xy + float2(dx * x, dy * y), input.lpos.z);
        }
    }
   
    shadowFactor /= kernelSize * kernelSize;
    shadowFactor = saturate(shadowFactor + 0.2f);

    return shadowFactor * saturate(specular + textureColor * diffuse);
}