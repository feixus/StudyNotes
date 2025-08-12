#include "RNG.hlsli"
#include "Common.hlsli"

#define RootSig "CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(UAV(u0, numDescriptors = 8), visibility=SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(SRV(t0, numDescriptors = 3), visibility=SHADER_VISIBILITY_ALL), " \
                "StaticSampler(s0, filter=FILTER_MIN_MAG_LINEAR_MIP_POINT, visibility=SHADER_VISIBILITY_ALL)"
                
struct ParticleData
{
    float3 Position;
    float LifeTime;
    float3 Velocity;
    float Size;
};

struct CS_INPUT
{
    uint3 GroupId : SV_GROUPID;
    uint3 GroupThreadId : SV_GROUPTHREADID;
    uint3 DispatchThreadId : SV_DISPATCHTHREADID;
    uint GroupIndex : SV_GROUPINDEX;
};

#define DEAD_LIST_COUNTER 0
#define ALIVE_LIST_1_COUNTER 4
#define ALIVE_LIST_2_COUNTER 8
#define EMIT_COUNT 12

cbuffer SimulationParameters : register(b0)
{
    int cEmitCount;
}

cbuffer EmitParameters : register(b0)
{
    float4 cRandomDirections[64];
}

cbuffer SimulateParameters : register(b0)
{
    float4x4 cViewProjection;
    float cDeltaTime;
    float cParticleLifetime;
    float cNear;
    float cFar;
}


RWByteAddressBuffer uCounters : register(u0);
RWByteAddressBuffer uEmitArguments : register(u1);
RWByteAddressBuffer uSimulateArguments : register(u2);
RWByteAddressBuffer uDrawArgumentsBuffer : register(u3);
RWStructuredBuffer<uint> uDeadList : register(u4);
RWStructuredBuffer<uint> uAliveList1 : register(u5);
RWStructuredBuffer<uint> uAliveList2 : register(u6);
RWStructuredBuffer<ParticleData> uParticleData : register(u7);

ByteAddressBuffer tCounters : register(t0);
Texture2D tDepth : register(t1);
Texture2D tNormals : register(t2);

SamplerState sSampler : register(s0);

[RootSignature(RootSig)]
[numthreads(1, 1, 1)]
void UpdateSimulationParameters(CS_INPUT input)
{
    uint deadCount = uCounters.Load(DEAD_LIST_COUNTER);
    uint aliveParticleCount = uCounters.Load(ALIVE_LIST_2_COUNTER);

    uint emitCount = min(deadCount, cEmitCount);

    uEmitArguments.Store3(0, uint3(ceil((float)emitCount / 128), 1, 1));
    uint simulateCount = ceil((float)(aliveParticleCount + emitCount) / 128);
    uSimulateArguments.Store3(0, uint3(simulateCount, 1, 1));

    uCounters.Store(ALIVE_LIST_1_COUNTER, aliveParticleCount);
    uCounters.Store(ALIVE_LIST_2_COUNTER, 0);
    uCounters.Store(EMIT_COUNT, emitCount);
}

[numthreads(128, 1, 1)]
void Emit(CS_INPUT input)
{
    uint emitCount = uCounters.Load(EMIT_COUNT);
    if (input.DispatchThreadId.x < emitCount)
    {
        uint deadSlot;
        uCounters.InterlockedAdd(DEAD_LIST_COUNTER, -1, deadSlot);
        uint particleIndex = uDeadList[deadSlot - 1];

        ParticleData p;
        p.LifeTime = 0;
        p.Position = float3(0, 3, 0);
        p.Velocity = 20 * cRandomDirections[particleIndex % 64].xyz;
        p.Size = (float)Random(deadSlot, 10, 30) / 100.0f;
        uParticleData[particleIndex] = p;

        uint aliveSlot;
        uCounters.InterlockedAdd(ALIVE_LIST_1_COUNTER, 1, aliveSlot);
        uAliveList1[aliveSlot] = particleIndex;
    }
}

[numthreads(128, 1, 1)]
void Simulate(CS_INPUT input)
{
    uint aliveCount = uCounters.Load(ALIVE_LIST_1_COUNTER);
    if (input.DispatchThreadId.x < aliveCount)
    {
        uint particleIndex = uAliveList1[input.DispatchThreadId.x];
        ParticleData p = uParticleData[particleIndex];
        if (p.LifeTime < cParticleLifetime)
        {
            float4 screenPos = mul(float4(p.Position, 1), cViewProjection);
            screenPos.xyz /= screenPos.w;
            if (screenPos.x > -1 && screenPos.x < 1 && screenPos.y > -1 && screenPos.y < 1)
            {
                float2 uv = screenPos.xy * float2(0.5f, -0.5f) + 0.5f;
                float depth = LinearizeDepth(tDepth.SampleLevel(sSampler, uv, 0).r, cFar, cNear);
                const float thickness = 0.2f;

                if (screenPos.w + p.Size > depth && screenPos.w - p.Size < depth + thickness)
                {
                    float3 normal = tNormals.SampleLevel(sSampler, uv, 0).xyz;
                    if (dot(normal, p.Velocity) < 0)
                    {
                        p.Velocity = reflect(p.Velocity, normal);
                    }
                }
            }

            p.Velocity += float3(0, -9.81f * cDeltaTime, 0);
            p.Position += p.Velocity * cDeltaTime;
            p.LifeTime += cDeltaTime;
            uParticleData[particleIndex] = p;

            uint aliveSlot;
            uCounters.InterlockedAdd(ALIVE_LIST_2_COUNTER, 1, aliveSlot);
            uAliveList2[aliveSlot] = particleIndex;
        }
        else
        {
            uint deadSlot;
            uCounters.InterlockedAdd(DEAD_LIST_COUNTER, 1, deadSlot);
            uDeadList[deadSlot] = particleIndex;
        }
    }
}

[numthreads(1, 1, 1)]
void SimulateEnd(CS_INPUT input)
{
    uint particleCount = uCounters.Load(ALIVE_LIST_2_COUNTER);
    uDrawArgumentsBuffer.Store4(0, uint4(6 * particleCount, 1, 0, 0));
}