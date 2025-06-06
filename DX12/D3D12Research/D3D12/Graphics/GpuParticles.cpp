#include "stdafx.h"
#include "GpuParticles.h"
#include "GraphicsBuffer.h"
#include "CommandSignature.h"
#include "Graphics.h"
#include "Shader.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "CommandContext.h"
#include "Profiler.h"
#include "GraphicsResource.h"

static constexpr uint32_t cMaxParticleCount = 2048;

struct ParticleData
{
    Vector3 Position;
    float Lifetime;
    Vector3 Velocity;
};

GpuParticles::GpuParticles(Graphics* pGraphics)
    : m_pGraphics(pGraphics)
{
}

void GpuParticles::Initialize()
{
    GraphicsCommandContext* pContext = static_cast<GraphicsCommandContext*>(m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT));

    m_pCounterBuffer = std::make_unique<ByteAddressBuffer>(m_pGraphics);
    m_pCounterBuffer->Create(m_pGraphics, sizeof(uint32_t), 4);

    m_pAliveList1 = std::make_unique<StructuredBuffer>(m_pGraphics);
    m_pAliveList1->Create(m_pGraphics, sizeof(uint32_t), cMaxParticleCount);
    m_pAliveList2 = std::make_unique<StructuredBuffer>(m_pGraphics);
    m_pAliveList2->Create(m_pGraphics, sizeof(uint32_t), cMaxParticleCount);
    m_pDeadList = std::make_unique<StructuredBuffer>(m_pGraphics);
    m_pDeadList->Create(m_pGraphics, sizeof(uint32_t), cMaxParticleCount);

    std::array<uint32_t, cMaxParticleCount> deadList;
    std::generate(deadList.begin(), deadList.end(), [n = 0]() mutable { return n++; });
    m_pDeadList->SetData(pContext, deadList.data(), sizeof(uint32_t) * deadList.size());

    uint32_t aliveCount = cMaxParticleCount;
    m_pCounterBuffer->SetData(pContext, &aliveCount, sizeof(uint32_t), 0);

    m_pParticleBuffer = std::make_unique<StructuredBuffer>(m_pGraphics);
    m_pParticleBuffer->Create(m_pGraphics, sizeof(ParticleData), cMaxParticleCount);

    m_pEmitArguments = std::make_unique<ByteAddressBuffer>(m_pGraphics);
    m_pEmitArguments->Create(m_pGraphics, sizeof(uint32_t), 3);
    m_pSimulateArguments = std::make_unique<ByteAddressBuffer>(m_pGraphics);
    m_pSimulateArguments->Create(m_pGraphics, sizeof(uint32_t), 3);

    pContext->Execute(true);

    m_pSimpleDispatchCommandSignature = std::make_unique<CommandSignature>();
    m_pSimpleDispatchCommandSignature->AddDispatch();
    m_pSimpleDispatchCommandSignature->Finalize("Simple Dispatch", m_pGraphics->GetDevice());

    {
        Shader computerShader("Resources/Shaders/ParticleSimulate.hlsl", Shader::Type::ComputeShader, "UpdateSimulationParameters", { "COMPILE_UPDATE_PARAMETERS" });
        m_pPrepareArgumentsRS = std::make_unique<RootSignature>();
        m_pPrepareArgumentsRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
        m_pPrepareArgumentsRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, D3D12_SHADER_VISIBILITY_ALL);
        m_pPrepareArgumentsRS->Finalize("Prepare Particle Arguments RS", m_pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_NONE);
        m_pPrepareArgumentsPSO = std::make_unique<ComputePipelineState>();
        m_pPrepareArgumentsPSO->SetComputeShader(computerShader.GetByteCode(), computerShader.GetByteCodeSize());
        m_pPrepareArgumentsPSO->SetRootSignature(m_pPrepareArgumentsRS->GetRootSignature());
        m_pPrepareArgumentsPSO->Finalize("Prepare Particle Arguments PSO", m_pGraphics->GetDevice());
    }

    {
        Shader computerShader("Resources/Shaders/ParticleSimulate.hlsl", Shader::Type::ComputeShader, "Emit", { "COMPILE_EMITTER" });
        m_pEmitRS = std::make_unique<RootSignature>();
        m_pEmitRS->SetDescriptorTableSimple(0, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 4, D3D12_SHADER_VISIBILITY_ALL);
        m_pEmitRS->Finalize("Particles Emit RS", m_pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_NONE);
        m_pEmitPSO = std::make_unique<ComputePipelineState>();
        m_pEmitPSO->SetComputeShader(computerShader.GetByteCode(), computerShader.GetByteCodeSize());
        m_pEmitPSO->SetRootSignature(m_pEmitRS->GetRootSignature());
        m_pEmitPSO->Finalize("Particles Emit PSO", m_pGraphics->GetDevice());
    }

    {
        Shader simulateShader("Resources/Shaders/ParticleSimulate.hlsl", Shader::Type::ComputeShader, "Simulate", { "COMPILE_SIMULATE" });
        m_pSimulateRS = std::make_unique<RootSignature>();
        m_pSimulateRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
        m_pSimulateRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 5, D3D12_SHADER_VISIBILITY_ALL);
        m_pSimulateRS->Finalize("Particles Simulate RS", m_pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_NONE);
        m_pSimulatePSO = std::make_unique<ComputePipelineState>();
        m_pSimulatePSO->SetComputeShader(simulateShader.GetByteCode(), simulateShader.GetByteCodeSize());
        m_pSimulatePSO->SetRootSignature(m_pSimulateRS->GetRootSignature());
        m_pSimulatePSO->Finalize("Particles Simulate PSO", m_pGraphics->GetDevice());        
    }

    {
		Shader vertexShader("Resources/Shaders/ParticleRendering.hlsl", Shader::Type::VertexShader, "VSMain");
		Shader geometryShader("Resources/Shaders/ParticleRendering.hlsl", Shader::Type::GeometryShader, "GSMain");
		Shader pixelShader("Resources/Shaders/ParticleRendering.hlsl", Shader::Type::PixelShader, "PSMain");

		m_pParticleRenderRS = std::make_unique<RootSignature>();
        m_pParticleRenderRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_GEOMETRY);
        m_pParticleRenderRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, D3D12_SHADER_VISIBILITY_GEOMETRY);
        m_pParticleRenderRS->Finalize("Particles Render RS", m_pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		m_pParticleRenderPSO = std::make_unique<GraphicsPipelineState>();
		m_pParticleRenderPSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pParticleRenderPSO->SetGeometryShader(geometryShader.GetByteCode(), geometryShader.GetByteCodeSize());
		m_pParticleRenderPSO->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
		m_pParticleRenderPSO->SetRootSignature(m_pParticleRenderRS->GetRootSignature());
        m_pParticleRenderPSO->SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
        m_pParticleRenderPSO->SetInputLayout(nullptr, 0);
        m_pParticleRenderPSO->SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount(), m_pGraphics->GetMultiSampleQualityLevel(m_pGraphics->GetMultiSampleCount()));
        m_pParticleRenderPSO->Finalize("Particles Render PSO", m_pGraphics->GetDevice());		
    }
}

void GpuParticles::Simulate()
{
    GraphicsCommandContext* pContext = static_cast<GraphicsCommandContext*>(m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT));
    {
        Profiler::Instance()->Begin("Prepare Arguments", pContext);
        pContext->SetComputePipelineState(m_pPrepareArgumentsPSO.get());
        pContext->SetComputeRootSignature(m_pPrepareArgumentsRS.get());

        pContext->InsertResourceBarrier(m_pEmitArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        pContext->InsertResourceBarrier(m_pSimulateArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        pContext->FlushResourceBarriers();

        struct Parameters
        {
            uint32_t EmitCount;
        } parameters;
        parameters.EmitCount = 5;

        D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {
            m_pCounterBuffer->GetUAV(),
            m_pEmitArguments->GetUAV(),
            m_pSimulateArguments->GetUAV()
        };

        pContext->SetComputeDynamicConstantBufferView(0, &parameters, sizeof(Parameters));
        pContext->SetDynamicDescriptors(1, 0, uavs, _countof(uavs));

        pContext->Dispatch(1, 1, 1);
        pContext->InsertUavBarrier();
        pContext->InsertResourceBarrier(m_pEmitArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        pContext->InsertResourceBarrier(m_pSimulateArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        pContext->FlushResourceBarriers();

        Profiler::Instance()->End(pContext);
    }

    {
        Profiler::Instance()->Begin("Emit Particles", pContext);
        pContext->SetComputePipelineState(m_pEmitPSO.get());
        pContext->SetComputeRootSignature(m_pEmitRS.get());

        D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {
            m_pCounterBuffer->GetUAV(),
            m_pDeadList->GetUAV(),
            m_pAliveList1->GetUAV(),
            m_pParticleBuffer->GetUAV()
        };
        pContext->SetDynamicDescriptors(0, 0, uavs, _countof(uavs));

        pContext->ExecuteIndirect(m_pSimpleDispatchCommandSignature->GetCommandSignature(), m_pEmitArguments.get());

        pContext->InsertUavBarrier();
        Profiler::Instance()->End(pContext);
    }

    {
        Profiler::Instance()->Begin("Simulate Particles", pContext);
        pContext->SetComputePipelineState(m_pSimulatePSO.get());
        pContext->SetComputeRootSignature(m_pSimulateRS.get());

        struct Parameters
        {
            float DeltaTime;
            float ParticleLifetime;
        } parameters;
        parameters.DeltaTime = GameTimer::DeltaTime();
        parameters.ParticleLifetime = 2.0f;

        pContext->SetComputeDynamicConstantBufferView(0, &parameters, sizeof(Parameters));

        D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {
            m_pCounterBuffer->GetUAV(),
            m_pDeadList->GetUAV(),
            m_pAliveList1->GetUAV(),
            m_pAliveList2->GetUAV(),
            m_pParticleBuffer->GetUAV()
        };
        pContext->SetDynamicDescriptors(1, 0, uavs, _countof(uavs));

        pContext->ExecuteIndirect(m_pSimpleDispatchCommandSignature->GetCommandSignature(), m_pSimulateArguments.get());

        pContext->InsertUavBarrier();
        Profiler::Instance()->End(pContext);
    }

    std::swap(m_pAliveList1, m_pAliveList2);
    pContext->Execute(true);
}

void GpuParticles::Render()
{
    
}
