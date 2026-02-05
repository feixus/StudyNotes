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

cbuffer FrameData : register(b0)
{
    float4x4 cViewInverse;
    float4x4 cView;
    float4x4 cProjection;
}

StructuredBuffer<ParticleData> tParticleData : register(t0);
StructuredBuffer<uint> tAliveList : register(t1);

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
    float4 color : COLOR;
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
PS_INPUT VSMain(uint vertexId : SV_VertexID)
{
    PS_INPUT output;

    uint vertexID = vertexId % 6;
    uint instanceId = vertexId / 6;

    uint particleIndex = tAliveList[instanceId];
    ParticleData particle = tParticleData[particleIndex];
    float3 quadPos = particle.Size * BILLBOARD[vertexID];

    output.position = float4(mul(quadPos, (float3x3)cViewInverse), 1);
    output.position.xyz += particle.Position;
    output.position = mul(output.position, cView);
    output.position = mul(output.position, cProjection);
    
    // Calculate color based on lifetime
    float lifeFactor = particle.LifeTime / 4.0f; // Assuming max lifetime is 4
    output.color = float4(1.0f - lifeFactor, lifeFactor, 0.0f, 1.0f); // Gradient from red to green
    output.texCoord = BILLBOARD[vertexID].xy * 0.5f + 0.5f;

    return output;
}

float4 PSMain(PS_INPUT input) : SV_TARGET
{
    float alpha = 1 - saturate(2 * length(input.texCoord - 0.5f));
    return float4(input.color.xyz, alpha);
}


