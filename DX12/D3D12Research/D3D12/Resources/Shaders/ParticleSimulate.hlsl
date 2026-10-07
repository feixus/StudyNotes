#include "Random.hlsli"
#include "CommonBindings.hlsli"

#define RootSig ROOT_SIG("CBV(b0), " \
                "DescriptorTable(UAV(u0, numDescriptors = 8)), " \
                "DescriptorTable(SRV(t0, numDescriptors = 2))")
                
#define DEAD_LIST_COUNTER 0
#define ALIVE_LIST_1_COUNTER 4
#define ALIVE_LIST_2_COUNTER 8
#define EMIT_COUNT 12

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

struct SimulationParameters
{
    int EmitCount;
};

struct EmitParameters
{
    float4 RandomDirections[64];
    float4 Origin;
};

struct SimulateParameters
{
    float4x4 ViewProjection;
    float4x4 ViewProjectionInv;
    float2 ViewDimensionsInv;
    float DeltaTime;
    float ParticleLifetime;
    float Near;
    float Far;
};

ConstantBuffer<SimulationParameters> cSimulationParameters : register(b0);
ConstantBuffer<EmitParameters> cEmitParameters : register(b0);
ConstantBuffer<SimulateParameters> cSimulateParameters : register(b0);

RWByteAddressBuffer uCounters : register(u0);
RWByteAddressBuffer uEmitArguments : register(u1);
RWByteAddressBuffer uSimulateArguments : register(u2);
RWByteAddressBuffer uDrawArgumentsBuffer : register(u3);
RWStructuredBuffer<uint> uDeadList : register(u4);
RWStructuredBuffer<uint> uAliveList1 : register(u5);
RWStructuredBuffer<uint> uAliveList2 : register(u6);
RWStructuredBuffer<ParticleData> uParticleData : register(u7);

ByteAddressBuffer tCounters : register(t0);
Texture2D tSceneDepth : register(t1);

float3 RandomDirection(uint seed)
{
    return normalize(float3(
        lerp(-0.8f, 0.8f, Random01(seed)),
        lerp(0.2f, 0.8f, Random01(seed)),
        lerp(0.2f, 0.8f, Random01(seed))
    ));
}

[RootSignature(RootSig)]
[numthreads(1, 1, 1)]
void UpdateSimulationParameters(CS_INPUT input)
{
    uint deadCount = uCounters.Load(DEAD_LIST_COUNTER);
    uint aliveParticleCount = uCounters.Load(ALIVE_LIST_2_COUNTER);

    uint emitCount = min(deadCount, cSimulationParameters.EmitCount);

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

        uint seed = SeedThread(deadSlot * particleIndex);

        ParticleData p;
        p.LifeTime = 0;
        p.Position = cEmitParameters.Origin.xyz;
        p.Velocity = (Random01(seed) + 1) * 30 * RandomDirection(seed);
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
        if (p.LifeTime < cSimulateParameters.ParticleLifetime)
        {
            float4 screenPos = mul(float4(p.Position, 1), cSimulateParameters.ViewProjection);
            screenPos.xyz /= screenPos.w;
            if (screenPos.x > -1 && screenPos.x < 1 && screenPos.y > -1 && screenPos.y < 1)
            {
                float2 uv = screenPos.xy * float2(0.5f, -0.5f) + 0.5f;
                float depth = tSceneDepth.SampleLevel(sLinearClamp, uv, 0).r;
                float linearDepth = LinearizeDepth(depth, cSimulateParameters.Near, cSimulateParameters.Far);
                const float thickness = 1.0f;

                if (screenPos.w + p.Size > linearDepth && screenPos.w - p.Size - thickness < linearDepth)
                {
                    float3 normal = NormalFromDepth(tSceneDepth, sLinearClamp, uv, cSimulateParameters.ViewDimensionsInv, cSimulateParameters.ViewProjectionInv);
                    if (dot(normal, p.Velocity) < 0)
                    {
                        p.Velocity = reflect(p.Velocity, normal) * 0.85f;
                    }
                }
            }

            p.Velocity += float3(0, -9.81f * cSimulateParameters.DeltaTime * 5, 0);
            p.Position += p.Velocity * cSimulateParameters.DeltaTime;
            p.LifeTime += cSimulateParameters.DeltaTime;
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
    uint particleCount = tCounters.Load(ALIVE_LIST_2_COUNTER);
    uDrawArgumentsBuffer.Store4(0, uint4(6 * particleCount, 1, 0, 0));
}