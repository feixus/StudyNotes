cbuffer test : register(b0) // b-const buffer t-texture s-sampler
{
    float4 color;
}

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

PSInput VSMain(float4 position : POSITION, float4 color : COLOR)
{
    PSInput result;
    
    result.position = position;
    result.color = color;
    
    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    //return input.color;
    return color;
}