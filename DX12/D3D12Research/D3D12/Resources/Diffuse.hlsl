cbuffer ObjectData : register(b0) // b-const buffer t-texture s-sampler
{
    float4x4 World;
    float4x4 WorldViewProjection;
}

cbuffer PerFrameData : register(b1)
{
    float4 LightPosition;
    float4x4 LightViewProjection;
}

SamplerState mySampler : register(s0);
Texture2D myTexture : register(t0);
Texture2D myShadowMap : register(t1);

struct VSInput
{
    float3 position : POSITION;
    float2 texCoord : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float4 lpos : TEXCOORD1;
};

PSInput VSMain(VSInput input)
{
    PSInput result;
    
    result.position = mul(float4(input.position, 1.0f), WorldViewProjection);
    result.texCoord = input.texCoord;
    result.normal = normalize(mul(input.normal, (float3x3)World));
    result.tangent = normalize(mul(input.tangent, (float3x3)World));
    result.lpos = mul(float4(input.position, 1.0f), mul(World, LightViewProjection));
    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 lightDirection = normalize(LightPosition.xyz);
    float diffuse = saturate(dot(lightDirection, input.normal));
    float4 textureColor = myTexture.Sample(mySampler, input.texCoord);
    
    // clip space via perspective divide to ndc space(positive Y is up), then to texture space(positive Y is down)
    input.lpos.xyz /= input.lpos.w;
    input.lpos.x = input.lpos.x / 2.0f + 0.5f;
    input.lpos.y = input.lpos.y / -2.0f + 0.5f;
    input.lpos.z -= 0.0001f;
    float depth = myShadowMap.Sample(mySampler, input.lpos.xy).r;
    if (depth < input.lpos.z)
    {
        diffuse *= 0.5f;
    }

    return textureColor * diffuse;
}