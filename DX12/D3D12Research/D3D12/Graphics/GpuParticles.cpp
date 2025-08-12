#include "stdafx.h"
#include "GpuParticles.h"
#include "Profiler.h"
#include "Scene/Camera.h"
#include "RenderGraph/RenderGraph.h"
#include "Graphics/Core/GraphicsBuffer.h"
#include "Graphics/Core/CommandSignature.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/GraphicsResource.h"
#include "Graphics/Core/GraphicsTexture.h"
#include "Graphics/Core/ResourceViews.h"
#include "Graphics/Core/OnlineDescriptorAllocator.h"
#include "ImGuiRenderer.h"

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
    : m_pGraphics(pGraphics)
{}

void GpuParticles::Initialize()
{
    CommandContext* pContext = m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);

    m_pCounterBuffer = std::make_unique<Buffer>(m_pGraphics, "GpuParticle CounterBuffer");
    m_pCounterBuffer->Create(BufferDesc::CreateByteAddress(4 * sizeof(uint32_t)));
    uint32_t aliveCount = cMaxParticleCount;
    m_pCounterBuffer->SetData(pContext, &aliveCount, sizeof(uint32_t), 0);

    BufferDesc particleBufferDesc = BufferDesc::CreateStructured(cMaxParticleCount, sizeof(uint32_t));
    m_pAliveList1 = std::make_unique<Buffer>(m_pGraphics, "GpuParticle AliveList1");
    m_pAliveList1->Create(particleBufferDesc);
    m_pAliveList2 = std::make_unique<Buffer>(m_pGraphics, "GpuParticle AliveList2");
    m_pAliveList2->Create(particleBufferDesc);

    m_pDeadList = std::make_unique<Buffer>(m_pGraphics, "GpuParticle DeadList");
    m_pDeadList->Create(particleBufferDesc);
    std::vector<uint32_t> deadList(cMaxParticleCount);
    std::generate(deadList.begin(), deadList.end(), [n = 0]() mutable { return n++; });
    m_pDeadList->SetData(pContext, deadList.data(), sizeof(uint32_t) * deadList.size());
    
    m_pParticleBuffer = std::make_unique<Buffer>(m_pGraphics, "GpuParticle ParticleBuffer");
    m_pParticleBuffer->Create(BufferDesc::CreateStructured(cMaxParticleCount, sizeof(ParticleData)));
    
    m_pEmitArguments = std::make_unique<Buffer>(m_pGraphics, "GpuParticle EmitArgs");
    m_pEmitArguments->Create(BufferDesc::CreateByteAddress(3 * sizeof(uint32_t)));
    m_pSimulateArguments = std::make_unique<Buffer>(m_pGraphics, "GpuParticle SimulateArgs");
    m_pSimulateArguments->Create(BufferDesc::CreateByteAddress(3 * sizeof(uint32_t)));
    m_pDrawArguments = std::make_unique<Buffer>(m_pGraphics, "GpuParticle RenderArgs");
    m_pDrawArguments->Create(BufferDesc::CreateByteAddress(4 * sizeof(uint32_t)));

    pContext->Execute(true);

    m_pSimpleDispatchCommandSignature = std::make_unique<CommandSignature>();
    m_pSimpleDispatchCommandSignature->AddDispatch();
    m_pSimpleDispatchCommandSignature->Finalize("Simple Dispatch", m_pGraphics->GetDevice());

    m_pSimpleDrawCommandSignature = std::make_unique<CommandSignature>();
    m_pSimpleDrawCommandSignature->AddDraw();
    m_pSimpleDrawCommandSignature->Finalize("Simple Draw", m_pGraphics->GetDevice());

    {
        Shader computerShader("Resources/Shaders/ParticleSimulate.hlsl", Shader::Type::Compute, "UpdateSimulationParameters");
        m_pSimulateRS = std::make_unique<RootSignature>();
        m_pSimulateRS->FinalizeFromShader("Particle Simulation RS", computerShader, m_pGraphics->GetDevice());
    }

    {
        Shader computerShader("Resources/Shaders/ParticleSimulate.hlsl", Shader::Type::Compute, "UpdateSimulationParameters");
        m_pPrepareArgumentsPSO = std::make_unique<PipelineState>();
        m_pPrepareArgumentsPSO->SetComputeShader(computerShader.GetByteCode(), computerShader.GetByteCodeSize());
        m_pPrepareArgumentsPSO->SetRootSignature(m_pSimulateRS->GetRootSignature());
        m_pPrepareArgumentsPSO->Finalize("Prepare Particle Arguments PSO", m_pGraphics->GetDevice());
    }

    {
        Shader computerShader("Resources/Shaders/ParticleSimulate.hlsl", Shader::Type::Compute, "Emit");
        m_pEmitPSO = std::make_unique<PipelineState>();
        m_pEmitPSO->SetComputeShader(computerShader.GetByteCode(), computerShader.GetByteCodeSize());
        m_pEmitPSO->SetRootSignature(m_pSimulateRS->GetRootSignature());
        m_pEmitPSO->Finalize("Particles Emit PSO", m_pGraphics->GetDevice());
    }

    {
        Shader simulateShader("Resources/Shaders/ParticleSimulate.hlsl", Shader::Type::Compute, "Simulate");
        m_pSimulatePSO = std::make_unique<PipelineState>();
        m_pSimulatePSO->SetComputeShader(simulateShader.GetByteCode(), simulateShader.GetByteCodeSize());
        m_pSimulatePSO->SetRootSignature(m_pSimulateRS->GetRootSignature());
        m_pSimulatePSO->Finalize("Particles Simulate PSO", m_pGraphics->GetDevice());        
    }

    {
        Shader simulateShader("Resources/Shaders/ParticleSimulate.hlsl", Shader::Type::Compute, "SimulateEnd");
        m_pSimulateEndPSO = std::make_unique<PipelineState>();
        m_pSimulateEndPSO->SetComputeShader(simulateShader.GetByteCode(), simulateShader.GetByteCodeSize());
        m_pSimulateEndPSO->SetRootSignature(m_pSimulateRS->GetRootSignature());
        m_pSimulateEndPSO->Finalize("Particles Simulate End PSO", m_pGraphics->GetDevice());
    }

    {
		Shader vertexShader("Resources/Shaders/ParticleRendering.hlsl", Shader::Type::Vertex, "VSMain");
		Shader pixelShader("Resources/Shaders/ParticleRendering.hlsl", Shader::Type::Pixel, "PSMain");

		m_pParticleRenderRS = std::make_unique<RootSignature>();
        m_pParticleRenderRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
        m_pParticleRenderRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, D3D12_SHADER_VISIBILITY_VERTEX);
        m_pParticleRenderRS->Finalize("Particles Render RS", m_pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		m_pParticleRenderPSO = std::make_unique<PipelineState>();
		m_pParticleRenderPSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pParticleRenderPSO->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
		m_pParticleRenderPSO->SetRootSignature(m_pParticleRenderRS->GetRootSignature());
        m_pParticleRenderPSO->SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        m_pParticleRenderPSO->SetInputLayout(nullptr, 0);
        m_pParticleRenderPSO->SetBlendMode(BlendMode::Alpha, false);
        m_pParticleRenderPSO->SetCullMode(D3D12_CULL_MODE_NONE);
        m_pParticleRenderPSO->SetDepthEnable(false);
        m_pParticleRenderPSO->SetDepthTest(D3D12_COMPARISON_FUNC_ALWAYS);
        m_pParticleRenderPSO->SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount(), m_pGraphics->GetMultiSampleQualityLevel(m_pGraphics->GetMultiSampleCount()));
        m_pParticleRenderPSO->Finalize("Particles Render PSO", m_pGraphics->GetDevice());		
    }

    m_pGraphics->GetImGui()->AddUpdateCallback(ImGuiCallbackDelegate::CreateLambda([]() {
        ImGui::Begin("Parameters");
        ImGui::Text("Particles");
        ImGui::Checkbox("Simulate", &g_Simulate);
        ImGui::SliderInt("Emit Count", &g_EmitCount, 0, 50);
        ImGui::SliderFloat("Life Time", &g_LifeTime, 0.f, 10.f);
        ImGui::End();
    }));
}

void GpuParticles::Simulate(CommandContext& context, GraphicsTexture* pResolvedDepth, GraphicsTexture* pNormals)
{
    if (!g_Simulate)
    {
        return;
    }

	context.InsertResourceBarrier(m_pDrawArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context.InsertResourceBarrier(m_pEmitArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context.InsertResourceBarrier(m_pSimulateArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context.InsertResourceBarrier(m_pCounterBuffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context.InsertResourceBarrier(m_pAliveList1.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context.InsertResourceBarrier(m_pAliveList2.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context.InsertResourceBarrier(m_pParticleBuffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		
    context.SetComputeRootSignature(m_pSimulateRS.get());

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
        pResolvedDepth->GetSRV(),
        pNormals->GetSRV(),
    };

    context.SetDynamicDescriptors(1, 0, uavs, std::size(uavs));
    context.SetDynamicDescriptors(2, 0, srvs, std::size(srvs));
    
    {
        GPU_PROFILE_SCOPE("Prepare Arguments", &context);

		context.SetPipelineState(m_pPrepareArgumentsPSO.get());

		struct Parameters
		{
			int32_t EmitCount;
		} parameters;
		parameters.EmitCount = g_EmitCount;

		context.SetComputeDynamicConstantBufferView(0, &parameters, sizeof(Parameters));

		context.Dispatch(1, 1, 1);
		context.InsertUavBarrier();
        context.InsertResourceBarrier(m_pEmitArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        context.InsertResourceBarrier(m_pSimulateArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        context.FlushResourceBarriers();
    }

    {
        GPU_PROFILE_SCOPE("Emit Particles", &context);

		context.SetPipelineState(m_pEmitPSO.get());

		std::array<Vector4, 64> randomDirections;
		std::generate(randomDirections.begin(), randomDirections.end(), []() {
			Vector4 v = Vector4(Math::RandVector());
			v.y = Math::Lerp(0.1f, 0.8f, (float)abs(v.y));
			v.Normalize();
			return v;
			});
		context.SetComputeDynamicConstantBufferView(0, randomDirections.data(), (uint32_t)(sizeof(Vector4) * randomDirections.size()));

		context.ExecuteIndirect(m_pSimpleDispatchCommandSignature->GetCommandSignature(), m_pEmitArguments.get());

		context.InsertUavBarrier();
    }

    {
        GPU_PROFILE_SCOPE("Simulate Particles", &context);

		context.SetPipelineState(m_pSimulatePSO.get());

		struct Parameters
		{
			Matrix ViewProjection;
			float DeltaTime;
			float ParticleLifetime;
			float Near;
			float Far;
		} parameters;
		parameters.DeltaTime = GameTimer::DeltaTime();
		parameters.ParticleLifetime = g_LifeTime;
		parameters.ViewProjection = m_pGraphics->GetCamera()->GetViewProjection();
		parameters.Near = m_pGraphics->GetCamera()->GetNear();
		parameters.Far = m_pGraphics->GetCamera()->GetFar();

		context.SetComputeDynamicConstantBufferView(0, &parameters, sizeof(Parameters));

		context.ExecuteIndirect(m_pSimpleDispatchCommandSignature->GetCommandSignature(), m_pSimulateArguments.get());

		context.InsertUavBarrier();
    }

    {
        GPU_PROFILE_SCOPE("Simulate End", &context);

		context.SetPipelineState(m_pSimulateEndPSO.get());

		context.Dispatch(1, 1, 1);

		context.InsertUavBarrier();
    }
 
    std::swap(m_pAliveList1, m_pAliveList2);
}

void GpuParticles::Render(CommandContext& context)
{
    GPU_PROFILE_SCOPE("Draw Particles", &context);

	context.InsertResourceBarrier(m_pDrawArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
	context.InsertResourceBarrier(m_pParticleBuffer.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.InsertResourceBarrier(m_pGraphics->GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET);

	context.BeginRenderPass(RenderPassInfo(m_pGraphics->GetCurrentRenderTarget(), RenderPassAccess::Load_Store, m_pGraphics->GetDepthStencil(), RenderPassAccess::Load_DontCare));

	context.SetPipelineState(m_pParticleRenderPSO.get());
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
	context.SetDynamicDescriptor(1, 1, m_pAliveList1->GetSRV());

	context.ExecuteIndirect(m_pSimpleDrawCommandSignature->GetCommandSignature(), m_pDrawArguments.get(), DescriptorTableType::Graphics);

	context.EndRenderPass();
}
