cbuffer Data : register(b0) // b-const buffer t-texture s-sampler
{
    float4x4 World;
    float4x4 WorldViewProjection;
}

Texture2D myTexture : register(t0);
SamplerState mySampler : register(s0);

struct VSInput
{
    float3 position : POSITION;
    float2 texCoord : TEXCOORD;
    float3 normal : NORMAL;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD;
    float3 normal : NORMAL;
};

PSInput VSMain(VSInput input)
{
    PSInput result;
    
    result.position = mul(float4(input.position, 1.0f), WorldViewProjection);
    result.texCoord = input.texCoord;
    result.normal = normalize(mul(input.normal, (float3x3) World));

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 lightDirection = -normalize(float3(-1, -1, 1));
    float diffuse = saturate(dot(lightDirection, input.normal));
    float4 textureColor = myTexture.Sample(mySampler, input.texCoord);
    return textureColor * diffuse;

}