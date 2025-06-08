struct ParticleData
{
    float3 Position;
    float LifeTime;
    float3 Velocity;
};

cbuffer FrameData : register(b0)
{
    float4x4 cViewInverse;
    float4x4 cView;
    float4x4 cProjection;
}

StructuredBuffer<ParticleData> tParticleData : register(t0);
StructuredBuffer<uint> tAliveList : register(t1);

struct VS_INPUT
{
    uint vertexId : SV_VertexID;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
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

PS_INPUT VSMain(VS_INPUT input)
{
    PS_INPUT output;

    uint vertexId = input.vertexId % 6;
    uint instanceId = input.vertexId / 6;

    uint particleIndex = tAliveList[instanceId];
    ParticleData particle = tParticleData[particleIndex];
    float3 quadPos = 0.1 * BILLBOARD[vertexId];

    output.position = float4(mul(quadPos, (float3x3)cViewInverse), 1);
    output.position.xyz += particle.Position;
    output.position = mul(output.position, cView);
    output.position = mul(output.position, cProjection);
    
    // Calculate color based on lifetime
    float lifeFactor = particle.LifeTime / 4.0f; // Assuming max lifetime is 4
    output.color = float4(1.0f - lifeFactor, lifeFactor, 0.0f, 1.0f); // Gradient from red to green
    return output;
}

float4 PSMain(PS_INPUT input) : SV_TARGET
{
    return input.color;
}


