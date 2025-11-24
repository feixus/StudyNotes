#include "stdafx.h"
#include "GpuParticles.h"
#include "Scene/Camera.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Core/GraphicsBuffer.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/GraphicsResource.h"
#include "Graphics/Core/GraphicsTexture.h"
#include "Graphics/Core/ResourceViews.h"
#include "Graphics/Core/OnlineDescriptorAllocator.h"
#include "Graphics/Profiler.h"
#include "Graphics/ImGuiRenderer.h"

static bool g_Enable = true;
static int32_t g_EmitCount = 30;
static float g_LifeTime = 4.0f;
static bool g_Simulate = true;

static constexpr uint32_t cMaxParticleCount = 102400;

struct ParticleData
{
    Vector3 Position;
    float Lifetime;
    Vector3 Velocity;
    float Size;
};

GpuParticles::GpuParticles(Graphics* pGraphics)
{
    Initialize(pGraphics);
}

void GpuParticles::Initialize(Graphics* pGraphics)
{
    CommandContext* pContext = pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);

    m_pCounterBuffer = std::make_unique<Buffer>(pGraphics, "GpuParticle CounterBuffer");
    m_pCounterBuffer->Create(BufferDesc::CreateByteAddress(4 * sizeof(uint32_t)));
    uint32_t aliveCount = cMaxParticleCount;
    m_pCounterBuffer->SetData(pContext, &aliveCount, sizeof(uint32_t), 0);

    BufferDesc particleBufferDesc = BufferDesc::CreateStructured(cMaxParticleCount, sizeof(uint32_t));
    m_pAliveList1 = std::make_unique<Buffer>(pGraphics, "GpuParticle AliveList1");
    m_pAliveList1->Create(particleBufferDesc);
    m_pAliveList2 = std::make_unique<Buffer>(pGraphics, "GpuParticle AliveList2");
    m_pAliveList2->Create(particleBufferDesc);

    m_pDeadList = std::make_unique<Buffer>(pGraphics, "GpuParticle DeadList");
    m_pDeadList->Create(particleBufferDesc);
    std::vector<uint32_t> deadList(cMaxParticleCount);
    std::generate(deadList.begin(), deadList.end(), [n = 0]() mutable { return n++; });
    m_pDeadList->SetData(pContext, deadList.data(), sizeof(uint32_t) * deadList.size());
    
    m_pParticleBuffer = std::make_unique<Buffer>(pGraphics, "GpuParticle ParticleBuffer");
    m_pParticleBuffer->Create(BufferDesc::CreateStructured(cMaxParticleCount, sizeof(ParticleData)));
    
    m_pEmitArguments = std::make_unique<Buffer>(pGraphics, "GpuParticle EmitArgs");
    m_pEmitArguments->Create(BufferDesc::CreateByteAddress(3 * sizeof(uint32_t)));
    m_pSimulateArguments = std::make_unique<Buffer>(pGraphics, "GpuParticle SimulateArgs");
    m_pSimulateArguments->Create(BufferDesc::CreateByteAddress(3 * sizeof(uint32_t)));
    m_pDrawArguments = std::make_unique<Buffer>(pGraphics, "GpuParticle RenderArgs");
    m_pDrawArguments->Create(BufferDesc::CreateByteAddress(4 * sizeof(uint32_t)));

    pContext->Execute(true);

    m_pSimpleDispatchCommandSignature = std::make_unique<CommandSignature>(pGraphics);
    m_pSimpleDispatchCommandSignature->AddDispatch();
    m_pSimpleDispatchCommandSignature->Finalize("Simple Dispatch");

    m_pSimpleDrawCommandSignature = std::make_unique<CommandSignature>(pGraphics);
    m_pSimpleDrawCommandSignature->AddDraw();
    m_pSimpleDrawCommandSignature->Finalize("Simple Draw");

    {
        Shader* pComputerShader = pGraphics->GetShaderManager()->GetShader("ParticleSimulate.hlsl", ShaderType::Compute, "UpdateSimulationParameters");
        m_pSimulateRS = std::make_unique<RootSignature>(pGraphics);
        m_pSimulateRS->FinalizeFromShader("Particle Simulation RS", pComputerShader);
    }

    {
        Shader* pComputerShader = pGraphics->GetShaderManager()->GetShader("ParticleSimulate.hlsl", ShaderType::Compute, "UpdateSimulationParameters");
        PipelineStateInitializer psoDesc;
        psoDesc.SetComputeShader(pComputerShader);
        psoDesc.SetRootSignature(m_pSimulateRS->GetRootSignature());
        psoDesc.SetName("Prepare Particle Arguments PSO");
        m_pPrepareArgumentsPSO = pGraphics->CreatePipeline(psoDesc);
    }

    {
        Shader* pComputerShader = pGraphics->GetShaderManager()->GetShader("ParticleSimulate.hlsl", ShaderType::Compute, "Emit");
        PipelineStateInitializer psoDesc;
        psoDesc.SetComputeShader(pComputerShader);
        psoDesc.SetRootSignature(m_pSimulateRS->GetRootSignature());
        psoDesc.SetName("Particles Emit PSO");
        m_pEmitPSO = pGraphics->CreatePipeline(psoDesc);
    }

    {
        Shader* pSimulateShader = pGraphics->GetShaderManager()->GetShader("ParticleSimulate.hlsl", ShaderType::Compute, "Simulate");
        PipelineStateInitializer psoDesc;
        psoDesc.SetComputeShader(pSimulateShader);
        psoDesc.SetRootSignature(m_pSimulateRS->GetRootSignature());
        psoDesc.SetName("Particles Simulate PSO");        
        m_pSimulatePSO = pGraphics->CreatePipeline(psoDesc);
    }

    {
        Shader* pSimulateShader = pGraphics->GetShaderManager()->GetShader("ParticleSimulate.hlsl", ShaderType::Compute, "SimulateEnd");
        PipelineStateInitializer psoDesc;
        psoDesc.SetComputeShader(pSimulateShader);
        psoDesc.SetRootSignature(m_pSimulateRS->GetRootSignature());
        psoDesc.SetName("Particles Simulate End PSO");
        m_pSimulateEndPSO = pGraphics->CreatePipeline(psoDesc);
    }

    {
		Shader* pVertexShader = pGraphics->GetShaderManager()->GetShader("ParticleRendering.hlsl", ShaderType::Vertex, "VSMain");
		Shader* pPixelShader = pGraphics->GetShaderManager()->GetShader("ParticleRendering.hlsl", ShaderType::Pixel, "PSMain");

		m_pParticleRenderRS = std::make_unique<RootSignature>(pGraphics);
        m_pParticleRenderRS->FinalizeFromShader("Particles Render RS", pVertexShader);
        
        PipelineStateInitializer psoDesc;
		psoDesc.SetVertexShader(pVertexShader);
		psoDesc.SetPixelShader(pPixelShader);
		psoDesc.SetRootSignature(m_pParticleRenderRS->GetRootSignature());
        psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        psoDesc.SetBlendMode(BlendMode::Alpha, false);
        psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
        psoDesc.SetDepthWrite(false);
        psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
        psoDesc.SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, pGraphics->GetMultiSampleCount());
        psoDesc.SetName("Particles Render PSO");		
		m_pParticleRenderPSO = pGraphics->CreatePipeline(psoDesc);
    }

    pGraphics->GetImGui()->AddUpdateCallback(ImGuiCallbackDelegate::CreateLambda([]() {
        ImGui::Begin("Parameters");
        ImGui::Text("Particles");
        ImGui::Checkbox("Enabled", &g_Enable);
        ImGui::Checkbox("Simulate", &g_Simulate);
        ImGui::SliderInt("Emit Count", &g_EmitCount, 0, cMaxParticleCount / 50);
        ImGui::SliderFloat("Life Time", &g_LifeTime, 0.f, 10.f);
        ImGui::End();
    }));
}

void GpuParticles::Simulate(RGGraph& graph, GraphicsTexture* pSourceDepth, const Camera& camera)
{
    if (!g_Simulate || !g_Enable)
    {
        return;
    }

	D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {
		m_pCounterBuffer->GetUAV()->GetDescriptor(),
		m_pEmitArguments->GetUAV()->GetDescriptor(),
		m_pSimulateArguments->GetUAV()->GetDescriptor(),
		m_pDrawArguments->GetUAV()->GetDescriptor(),
		m_pDeadList->GetUAV()->GetDescriptor(),
		m_pAliveList1->GetUAV()->GetDescriptor(),
		m_pAliveList2->GetUAV()->GetDescriptor(),
		m_pParticleBuffer->GetUAV()->GetDescriptor(),
	};

	D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
		m_pCounterBuffer->GetSRV()->GetDescriptor(),
		pSourceDepth->GetSRV()->GetDescriptor(),
	};

    RG_GRAPH_SCOPE("Particle Simulation", graph);

    RGPassBuilder prepareArguments = graph.AddPass("Prepare Arguments");
    prepareArguments.Bind([=](CommandContext& context, const RGPassResource& passResources)
    {
            m_ParticlesToSpawn += (float)g_EmitCount * Time::DeltaTime();

	        context.InsertResourceBarrier(m_pDrawArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	        context.InsertResourceBarrier(m_pEmitArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	        context.InsertResourceBarrier(m_pSimulateArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	        context.InsertResourceBarrier(m_pCounterBuffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	        context.InsertResourceBarrier(m_pAliveList1.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	        context.InsertResourceBarrier(m_pAliveList2.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	        context.InsertResourceBarrier(m_pParticleBuffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		
            context.SetComputeRootSignature(m_pSimulateRS.get());
            context.BindResources(1, 0, uavs, (int)std::size(uavs));
            context.BindResources(2, 0, srvs, (int)std::size(srvs));

			context.SetPipelineState(m_pPrepareArgumentsPSO);

			struct Parameters
			{
				uint32_t EmitCount;
			} parameters;
			parameters.EmitCount = (uint32_t)floor(m_ParticlesToSpawn);
            m_ParticlesToSpawn -= parameters.EmitCount;

			context.SetComputeDynamicConstantBufferView(0, parameters);

			context.Dispatch(1, 1, 1);
			context.InsertUavBarrier();
			context.InsertResourceBarrier(m_pEmitArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
			context.InsertResourceBarrier(m_pSimulateArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
			context.FlushResourceBarriers();
        });

    RGPassBuilder emit = graph.AddPass("Emit");
    emit.Bind([=](CommandContext& context, const RGPassResource& passResources)
        {
			context.SetComputeRootSignature(m_pSimulateRS.get());
			context.BindResources(1, 0, uavs, (int)std::size(uavs));
			context.BindResources(2, 0, srvs, (int)std::size(srvs));

		    context.SetPipelineState(m_pEmitPSO);

            struct Parameters
            {
                std::array<Vector4, 64> RandomDirections;
                Vector3 Origin;
            };
            Parameters parameters{};

		    std::generate(parameters.RandomDirections.begin(), parameters.RandomDirections.end(), []() {
			    Vector4 v = Vector4(Math::RandVector());
			    v.y = Math::Lerp(0.6f, 0.8f, (float)abs(v.y));
			    v.z = Math::Lerp(0.6f, 0.8f, (float)abs(v.z));
			    v.Normalize();
			    return v;
			    });
            parameters.Origin = Vector3(150, 3, 0);

		    context.SetComputeDynamicConstantBufferView(0, parameters);
		    context.ExecuteIndirect(m_pSimpleDispatchCommandSignature.get(), 1, m_pEmitArguments.get(), m_pEmitArguments.get());
		    context.InsertUavBarrier();
        });

	RGPassBuilder simulate = graph.AddPass("Simulate");
    simulate.Bind([=](CommandContext& context, const RGPassResource& passResources)
        {
		    context.SetComputeRootSignature(m_pSimulateRS.get());
		    context.BindResources(1, 0, uavs, (int)std::size(uavs));
		    context.BindResources(2, 0, srvs, (int)std::size(srvs));

		    context.SetPipelineState(m_pSimulatePSO);

		    struct Parameters
		    {
			    Matrix ViewProjection;
                Matrix ViewProjectionInv;
                Vector2 DimensionsInv;
			    float DeltaTime{0};
			    float ParticleLifetime{0};
			    float Near{0};
			    float Far{0};
		    } parameters;
		    parameters.DeltaTime = Time::DeltaTime();
		    parameters.ParticleLifetime = g_LifeTime;
		    parameters.ViewProjection = camera.GetViewProjection();
            parameters.DimensionsInv.x = 1.0f / pSourceDepth->GetWidth();
            parameters.DimensionsInv.y = 1.0f / pSourceDepth->GetHeight();
            parameters.ViewProjectionInv = camera.GetViewProjectionInverse();
		    parameters.Near = camera.GetNear();
		    parameters.Far = camera.GetFar();

		    context.SetComputeDynamicConstantBufferView(0, parameters);
		    context.ExecuteIndirect(m_pSimpleDispatchCommandSignature.get(), 1, m_pSimulateArguments.get(), nullptr);
		    context.InsertUavBarrier();
        });

	RGPassBuilder simulateEnd = graph.AddPass("Simulate End");
    simulateEnd.Bind([=](CommandContext& context, const RGPassResource& passResources)
        {
            context.InsertResourceBarrier(m_pCounterBuffer.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		    context.SetComputeRootSignature(m_pSimulateRS.get());
		    context.BindResources(1, 0, uavs, (int)std::size(uavs));
		    context.BindResources(2, 0, srvs, (int)std::size(srvs));

		    context.SetPipelineState(m_pSimulateEndPSO);
		    context.Dispatch(1, 1, 1);
		    context.InsertUavBarrier();
        });
 
    std::swap(m_pAliveList1, m_pAliveList2);
}

void GpuParticles::Render(RGGraph& graph, GraphicsTexture* pTarget, GraphicsTexture* pDepth, const Camera& camera)
{
    if (!g_Enable)
    {
        return;
    }

	RGPassBuilder renderParticles = graph.AddPass("Render Particles");
    renderParticles.Bind([=](CommandContext& context, const RGPassResource& passResources)
        {
			context.InsertResourceBarrier(m_pDrawArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
			context.InsertResourceBarrier(m_pParticleBuffer.get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pAliveList1.get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
			context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);

			context.BeginRenderPass(RenderPassInfo(pTarget, RenderPassAccess::Load_Store, pDepth, RenderPassAccess::Load_Store, false));

			context.SetPipelineState(m_pParticleRenderPSO);
			context.SetGraphicsRootSignature(m_pParticleRenderRS.get());

			struct FrameData
			{
				Matrix ViewInverse;
				Matrix View;
				Matrix Projection;
			} frameData;
			frameData.ViewInverse = camera.GetViewInverse();
			frameData.View = camera.GetView();
			frameData.Projection = camera.GetProjection();

			context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context.SetGraphicsDynamicConstantBufferView(0, frameData);

			context.BindResource(1, 0, m_pParticleBuffer->GetSRV());
			context.BindResource(1, 1, m_pAliveList1->GetSRV());

			context.ExecuteIndirect(m_pSimpleDrawCommandSignature.get(), 1, m_pDrawArguments.get(), nullptr);
			context.EndRenderPass();
        });
}
