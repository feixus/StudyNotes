#include "CommonBindings.hlsli"

#define RootSig ROOT_SIG("RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                "CBV(b0, visibility = SHADER_VISIBILITY_VERTEX), " \
                "DescriptorTable(SRV(t0, numDescriptors = 2), visibility = SHADER_VISIBILITY_VERTEX)")
                
struct ParticleData
{
    float3 Position;
    float LifeTime;
    float3 Velocity;
    float Size;
};

struct PassParameters
{
    float4x4 ViewInverse;
    float4x4 View;
    float4x4 Projection;
};

ConstantBuffer<PassParameters> cPassParameters : register(b0);
StructuredBuffer<ParticleData> tParticleData : register(t0);
StructuredBuffer<uint> tAliveList : register(t1);

struct InterpolantsVSToPS
{
    float4 Position : SV_Position;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR;
};

static const float3 BILLBOARD[] = {
    float3(-1, -1, 0),
    float3( 1, -1, 0),
    float3(-1,  1, 0),

    float3(-1,  1, 0),
    float3( 1, -1, 0),
    float3( 1,  1, 0),
};

[RootSignature(RootSig)]
InterpolantsVSToPS VSMain(uint vertexId : SV_VertexID)
{
    InterpolantsVSToPS output;

    uint vertexID = vertexId % 6;
    uint instanceId = vertexId / 6;

    uint particleIndex = tAliveList[instanceId];
    ParticleData particle = tParticleData[particleIndex];
    float3 quadPos = particle.Size * BILLBOARD[vertexID];

    output.Position = float4(mul(quadPos, (float3x3)cPassParameters.ViewInverse), 1);
    output.Position.xyz += particle.Position;
    output.Position = mul(output.Position, cPassParameters.View);
    output.Position = mul(output.Position, cPassParameters.Projection);
    
    // Calculate color based on lifetime
    float lifeFactor = particle.LifeTime / 4.0f; // Assuming max lifetime is 4
    output.Color = float4(1.0f - lifeFactor, lifeFactor, 0.0f, 1.0f); // Gradient from red to green
    output.UV = BILLBOARD[vertexID].xy * 0.5f + 0.5f;

    return output;
}

float4 PSMain(InterpolantsVSToPS input) : SV_TARGET
{
    float alpha = 1 - saturate(2 * length(input.UV - 0.5f));
    return float4(input.Color.xyz, alpha);
}


