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
#include "Scene/Camera.h"
#include "GraphicsTexture.h"
#include "ResourceViews.h"
#include "OnlineDescriptorAllocator.h"
#include "RenderGraph/RenderGraph.h"

static constexpr uint32_t cMaxParticleCount = 102400;

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
    CommandContext* pContext = m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);

    m_pCounterBuffer = std::make_unique<Buffer>(m_pGraphics, "GpuParticle CounterBuffer");
    m_pCounterBuffer->Create(BufferDesc::CreateByteAddress(4 * sizeof(uint32_t)));
    uint32_t aliveCount = cMaxParticleCount;
    m_pCounterBuffer->SetData(pContext, &aliveCount, sizeof(uint32_t), 0);

    m_pEmitArguments = std::make_unique<Buffer>(m_pGraphics, "GpuParticle EmitArguments");
    m_pEmitArguments->Create(BufferDesc::CreateByteAddress(3 * sizeof(uint32_t)));
    m_pSimulateArguments = std::make_unique<Buffer>(m_pGraphics, "GpuParticle SimulateArguments");
    m_pSimulateArguments->Create(BufferDesc::CreateByteAddress(3 * sizeof(uint32_t)));

    m_pAliveList1 = std::make_unique<Buffer>(m_pGraphics, "GpuParticle AliveList1");
    m_pAliveList1->Create(BufferDesc::CreateStructured(cMaxParticleCount, sizeof(uint32_t)));
    m_pAliveList2 = std::make_unique<Buffer>(m_pGraphics, "GpuParticle AliveList2");
    m_pAliveList2->Create(BufferDesc::CreateStructured(cMaxParticleCount, sizeof(uint32_t)));
    m_pDeadList = std::make_unique<Buffer>(m_pGraphics, "GpuParticle DeadList");
    m_pDeadList->Create(BufferDesc::CreateStructured(cMaxParticleCount, sizeof(uint32_t)));

    std::vector<uint32_t> deadList(cMaxParticleCount);
    std::generate(deadList.begin(), deadList.end(), [n = 0]() mutable { return n++; });
    m_pDeadList->SetData(pContext, deadList.data(), sizeof(uint32_t) * deadList.size());

    m_pParticleBuffer = std::make_unique<Buffer>(m_pGraphics, "GpuParticle ParticleBuffer");
    m_pParticleBuffer->Create(BufferDesc::CreateStructured(cMaxParticleCount, sizeof(ParticleData)));

    m_pDrawArguments = std::make_unique<Buffer>(m_pGraphics, "GpuParticle Arguments");
    m_pDrawArguments->Create(BufferDesc::CreateByteAddress(4 * sizeof(uint32_t)));

    pContext->Execute(true);

    m_pSimpleDispatchCommandSignature = std::make_unique<CommandSignature>();
    m_pSimpleDispatchCommandSignature->AddDispatch();
    m_pSimpleDispatchCommandSignature->Finalize("Simple Dispatch", m_pGraphics->GetDevice());

    m_pSimpleDrawCommandSignature = std::make_unique<CommandSignature>();
    m_pSimpleDrawCommandSignature->AddDraw();
    m_pSimpleDrawCommandSignature->Finalize("Simple Draw", m_pGraphics->GetDevice());

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
        m_pEmitRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
        m_pEmitRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 4, D3D12_SHADER_VISIBILITY_ALL);
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
        Shader simulateShader("Resources/Shaders/ParticleSimulate.hlsl", Shader::Type::ComputeShader, "SimulateEnd", { "COMPILE_SIMULATE_END" });
        m_pSimulateEndRS = std::make_unique<RootSignature>();
        m_pSimulateEndRS->SetDescriptorTableSimple(0, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, D3D12_SHADER_VISIBILITY_ALL);
        m_pSimulateEndRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
        m_pSimulateEndRS->Finalize("Particles Simulate End RS", m_pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_NONE);
        m_pSimulateEndPSO = std::make_unique<ComputePipelineState>();
        m_pSimulateEndPSO->SetComputeShader(simulateShader.GetByteCode(), simulateShader.GetByteCodeSize());
        m_pSimulateEndPSO->SetRootSignature(m_pSimulateEndRS->GetRootSignature());
        m_pSimulateEndPSO->Finalize("Particles Simulate End PSO", m_pGraphics->GetDevice());
    }

    {
		Shader vertexShader("Resources/Shaders/ParticleRendering.hlsl", Shader::Type::VertexShader, "VSMain");
		Shader pixelShader("Resources/Shaders/ParticleRendering.hlsl", Shader::Type::PixelShader, "PSMain");

		m_pParticleRenderRS = std::make_unique<RootSignature>();
        m_pParticleRenderRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
        m_pParticleRenderRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, D3D12_SHADER_VISIBILITY_VERTEX);
        m_pParticleRenderRS->Finalize("Particles Render RS", m_pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		m_pParticleRenderPSO = std::make_unique<GraphicsPipelineState>();
		m_pParticleRenderPSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pParticleRenderPSO->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
		m_pParticleRenderPSO->SetRootSignature(m_pParticleRenderRS->GetRootSignature());
        m_pParticleRenderPSO->SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        m_pParticleRenderPSO->SetInputLayout(nullptr, 0);
        m_pParticleRenderPSO->SetCullMode(D3D12_CULL_MODE_NONE);
        m_pParticleRenderPSO->SetDepthEnable(false);
        m_pParticleRenderPSO->SetDepthTest(D3D12_COMPARISON_FUNC_ALWAYS);
        m_pParticleRenderPSO->SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount(), m_pGraphics->GetMultiSampleQualityLevel(m_pGraphics->GetMultiSampleCount()));
        m_pParticleRenderPSO->Finalize("Particles Render PSO", m_pGraphics->GetDevice());		
    }
}

void GpuParticles::Simulate(RGGraph& graph)
{
	graph.AddPass("Prepare Arguments", [&](RGPassBuilder& builder)
		{
			builder.NeverCull();
			return[=](CommandContext& context, const RGPassResource& passResources)
            {
                    context.SetComputePipelineState(m_pPrepareArgumentsPSO.get());
                    context.SetComputeRootSignature(m_pPrepareArgumentsRS.get());

                    context.InsertResourceBarrier(m_pCounterBuffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    context.InsertResourceBarrier(m_pEmitArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    context.InsertResourceBarrier(m_pSimulateArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    context.InsertResourceBarrier(m_pAliveList2.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    context.InsertResourceBarrier(m_pParticleBuffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    context.FlushResourceBarriers();

                    struct Parameters
                    {
                        uint32_t EmitCount;
                    } parameters;
                    parameters.EmitCount = 1024;

                    D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {
                        m_pCounterBuffer->GetUAV()->GetDescriptor(),
                        m_pEmitArguments->GetUAV()->GetDescriptor(),
                        m_pSimulateArguments->GetUAV()->GetDescriptor()
                    };

                    context.SetComputeDynamicConstantBufferView(0, &parameters, sizeof(Parameters));
                    context.SetDynamicDescriptors(1, 0, uavs, _countof(uavs));

                    context.Dispatch(1, 1, 1);
                    context.InsertUavBarrier();
                    context.InsertResourceBarrier(m_pEmitArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
                    context.InsertResourceBarrier(m_pSimulateArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
                    context.FlushResourceBarriers();
            };
         });

	graph.AddPass("Emit Particles", [&](RGPassBuilder& builder)
		{
			builder.NeverCull();
			return[=](CommandContext& context, const RGPassResource& passResources)
            {
                    context.SetComputePipelineState(m_pEmitPSO.get());
                    context.SetComputeRootSignature(m_pEmitRS.get());

                    D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {
                        m_pCounterBuffer->GetUAV()->GetDescriptor(),
                        m_pDeadList->GetUAV()->GetDescriptor(),
                        m_pAliveList1->GetUAV()->GetDescriptor(),
                        m_pParticleBuffer->GetUAV()->GetDescriptor()
                    };
                    context.SetDynamicDescriptors(1, 0, uavs, _countof(uavs));

                    std::array<Vector4, 64> randomDirections;
                    std::generate(randomDirections.begin(), randomDirections.end(), []() {
                        Vector3 v = Math::RandVector(); v.Normalize(); return Vector4(v.x, v.y, v.z, 0);
                        });
                    context.SetComputeDynamicConstantBufferView(0, randomDirections.data(), (uint32_t)(sizeof(Vector4) * randomDirections.size()));

                    context.ExecuteIndirect(m_pSimpleDispatchCommandSignature->GetCommandSignature(), m_pEmitArguments.get());

                    context.InsertUavBarrier();
            };
        });

	graph.AddPass("Simulate Particles", [&](RGPassBuilder& builder)
		{
			builder.NeverCull();
			return[=](CommandContext& context, const RGPassResource& passResources)
            {
					context.SetComputePipelineState(m_pSimulatePSO.get());
                    context.SetComputeRootSignature(m_pSimulateRS.get());

                    struct Parameters
                    {
                        float DeltaTime;
                        float ParticleLifetime;
                    } parameters;
                    parameters.DeltaTime = GameTimer::DeltaTime();
                    parameters.ParticleLifetime = 4.0f;

                    context.SetComputeDynamicConstantBufferView(0, &parameters, sizeof(Parameters));

                    D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {
                        m_pCounterBuffer->GetUAV()->GetDescriptor(),
                        m_pDeadList->GetUAV()->GetDescriptor(),
                        m_pAliveList1->GetUAV()->GetDescriptor(),
                        m_pAliveList2->GetUAV()->GetDescriptor(),
                        m_pParticleBuffer->GetUAV()->GetDescriptor()
                    };
                    context.SetDynamicDescriptors(1, 0, uavs, _countof(uavs));

                    context.ExecuteIndirect(m_pSimpleDispatchCommandSignature->GetCommandSignature(), m_pSimulateArguments.get());

                    context.InsertUavBarrier();
            };
        });

 
	graph.AddPass("Simulate End", [&](RGPassBuilder& builder)
		{
			builder.NeverCull();
			return[=](CommandContext& context, const RGPassResource& passResources)
            {
                context.InsertResourceBarrier(m_pDrawArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                context.InsertResourceBarrier(m_pCounterBuffer.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

                context.SetComputePipelineState(m_pSimulateEndPSO.get());
                context.SetComputeRootSignature(m_pSimulateEndRS.get());

                context.SetDynamicDescriptor(0, 0, m_pCounterBuffer->GetSRV());
                context.SetDynamicDescriptor(1, 0, m_pDrawArguments->GetUAV());

                context.Dispatch(1, 1, 1);

                context.InsertUavBarrier();
            };
        });
   
	graph.AddPass("Draw Particles", [&](RGPassBuilder& builder)
		{
			builder.NeverCull();
			return[=](CommandContext& context, const RGPassResource& passResources)
            {
                    context.InsertResourceBarrier(m_pDrawArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
                    context.InsertResourceBarrier(m_pAliveList2.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    context.InsertResourceBarrier(m_pParticleBuffer.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    context.InsertResourceBarrier(m_pGraphics->GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET);

                    context.BeginRenderPass(RenderPassInfo(m_pGraphics->GetCurrentRenderTarget(), RenderPassAccess::Load_Store, m_pGraphics->GetDepthStencil(), RenderPassAccess::Load_Store));

                    context.SetGraphicsPipelineState(m_pParticleRenderPSO.get());
                    context.SetGraphicsRootSignature(m_pParticleRenderRS.get());

                    Vector2 screenDimensions((float)m_pGraphics->GetWindowWidth(), (float)m_pGraphics->GetWindowHeight());
                    context.SetViewport(FloatRect(0, 0, screenDimensions.x, screenDimensions.y));
                    context.SetScissorRect(FloatRect(0, 0, screenDimensions.x, screenDimensions.y));

                    struct FrameData
                    {
                        Matrix ViewInverse;
                        Matrix View;
                        Matrix Projection;
                    } frameData;
                    frameData.ViewInverse = m_pGraphics->GetCamera()->GetViewInverse();
                    frameData.View = m_pGraphics->GetCamera()->GetView();
                    frameData.Projection = m_pGraphics->GetCamera()->GetProjection();

                    context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    context.SetDynamicConstantBufferView(0, &frameData, sizeof(FrameData));

                    context.SetDynamicDescriptor(1, 0, m_pParticleBuffer->GetSRV());
                    context.SetDynamicDescriptor(1, 1, m_pAliveList2->GetSRV());

                    context.ExecuteIndirect(m_pSimpleDrawCommandSignature->GetCommandSignature(), m_pDrawArguments.get(), DescriptorTableType::Graphics);

                    context.EndRenderPass();
            };
        });

    std::swap(m_pAliveList1, m_pAliveList2);
}

void GpuParticles::Render()
{
    
}
