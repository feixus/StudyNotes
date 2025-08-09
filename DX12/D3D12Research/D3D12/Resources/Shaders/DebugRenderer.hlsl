#include "Common.hlsli"

#define RootSig "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                "CBV(b0, visibility=SHADER_VISIBILITY_VERTEX)"

cbuffer Parameters : register(b0)
{
    float4x4 cViewProj;
}

struct VS_Input
{
    float3 position : POSITION;
    uint color : COLOR;
};

struct PS_Input
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

[RootSignature(RootSig)]
PS_Input VSMain(VS_Input input)
{
    PS_Input output;
    output.position = mul(float4(input.position, 1.0), cViewProj);
    output.color = UIntToColor(input.color);
    return output;
}

float4 PSMain(PS_Input input) : SV_TARGET
{
    return input.color;
}
