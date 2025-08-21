#include "stdafx.h"
#include "SSAO.h"
#include "Scene/Camera.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/GraphicsTexture.h"
#include "Graphics/Core/GraphicsBuffer.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/RenderGraph/RenderGraph.h"

SSAO::SSAO(Graphics* pGraphics)
{
	SetupResources(pGraphics);
	SetupPipelines(pGraphics);
}

void SSAO::OnSwapchainCreated(int widowWidth, int windowHeight)
{
	m_pAmbientOcclusionIntermediate->Create(TextureDesc::Create2D(Math::DivideAndRoundUp(widowWidth, 2), Math::DivideAndRoundUp(windowHeight, 2), DXGI_FORMAT_R8_UNORM, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess));
}

void SSAO::Execute(RGGraph& graph, GraphicsTexture* pColor, GraphicsTexture* pDepth, Camera& camera)
{
	float g_AoPower = 3;
	float g_AoThreshold = 0.0025f;
	float g_AoRadius = 0.5;
	int g_AoSamples = 16;

	ImGui::Begin("Parameters");
	ImGui::Text("Ambient Occlusion");
	ImGui::SliderFloat("Power", &g_AoPower, 0, 10);
	ImGui::SliderFloat("Threshold", &g_AoThreshold, 0.0001f, 0.01f);
	ImGui::SliderFloat("Radius", &g_AoRadius, 0, 2);
	ImGui::SliderInt("Samples", &g_AoSamples, 1, 64);
	ImGui::End();

	RGPassBuilder ssao = graph.AddPass("SSAO");
	ssao.Bind([=](CommandContext& renderContext, const RGPassResource& resources)
		{
			renderContext.InsertResourceBarrier(pDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			renderContext.InsertResourceBarrier(pColor, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			renderContext.SetComputeRootSignature(m_pSSAORS.get());
			renderContext.SetPipelineState(m_pSSAOPSO.get());

			constexpr int ssaoRandomVectors = 64;
			struct ShaderParameters
			{
				Matrix ProjectionInverse;
				Matrix ViewInverse;
				Matrix Projection;
				Matrix View;
				IntVector2 Dimensions;
				float Near{ 0.0f };
				float Far{ 0.0f };
				float Power{ 0.0f };
				float Radius{ 0.0f };
				float Threshold{ 0.0f };
				int Samples{ 0 };
			} shaderParameters{};

			shaderParameters.ProjectionInverse = camera.GetProjectionInverse();
			shaderParameters.ViewInverse = camera.GetViewInverse();
			shaderParameters.Projection = camera.GetProjection();
			shaderParameters.View = camera.GetView();
			shaderParameters.Dimensions.x = pColor->GetWidth();
			shaderParameters.Dimensions.y = pColor->GetHeight();
			shaderParameters.Near = camera.GetNear();
			shaderParameters.Far = camera.GetFar();
			shaderParameters.Power = g_AoPower;
			shaderParameters.Radius = g_AoRadius;
			shaderParameters.Threshold = g_AoThreshold;
			shaderParameters.Samples = g_AoSamples;

			renderContext.SetComputeDynamicConstantBufferView(0, &shaderParameters, sizeof(ShaderParameters));
			renderContext.SetDynamicDescriptor(1, 0, pColor->GetUAV());
			renderContext.SetDynamicDescriptor(2, 0, pDepth->GetSRV());

			int dispatchGroupX = Math::DivideAndRoundUp(pColor->GetWidth(), 16);
			int dispatchGroupY = Math::DivideAndRoundUp(pColor->GetHeight(), 16);
			renderContext.Dispatch(dispatchGroupX, dispatchGroupY);
		});

	RGPassBuilder blurSSAO = graph.AddPass("Blur SSAO");
	blurSSAO.Bind([=](CommandContext& renderContext, const RGPassResource& resources)
		{
			renderContext.InsertResourceBarrier(m_pAmbientOcclusionIntermediate.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			renderContext.InsertResourceBarrier(pColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

			renderContext.SetComputeRootSignature(m_pSSAOBlurRS.get());
			renderContext.SetPipelineState(m_pSSAOBlurPSO.get());

			struct ShaderParameters
			{
				Vector2 Dimensions;
				uint32_t Horizontal{ 0 };
				float Far{ 0.0f };
				float Near{ 0.0f };
			} shaderParameters;

			shaderParameters.Horizontal = 1;
			shaderParameters.Dimensions.x = 1.0f / pColor->GetWidth();
			shaderParameters.Dimensions.y = 1.0f / pColor->GetHeight();
			shaderParameters.Far = camera.GetFar();
			shaderParameters.Near = camera.GetNear();

			renderContext.SetComputeDynamicConstantBufferView(0, &shaderParameters, sizeof(ShaderParameters));
			renderContext.SetDynamicDescriptor(1, 0, m_pAmbientOcclusionIntermediate->GetUAV());
			renderContext.SetDynamicDescriptor(2, 0, pDepth->GetSRV());
			renderContext.SetDynamicDescriptor(2, 1, pColor->GetSRV());

			renderContext.Dispatch(Math::DivideAndRoundUp(m_pAmbientOcclusionIntermediate->GetWidth(), 256), m_pAmbientOcclusionIntermediate->GetHeight());

			renderContext.InsertResourceBarrier(m_pAmbientOcclusionIntermediate.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			renderContext.InsertResourceBarrier(pColor, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			renderContext.SetDynamicDescriptor(1, 0, pColor->GetUAV());
			renderContext.SetDynamicDescriptor(2, 0, pDepth->GetSRV());
			renderContext.SetDynamicDescriptor(2, 1, m_pAmbientOcclusionIntermediate->GetSRV());

			shaderParameters.Horizontal = 0;
			renderContext.SetComputeDynamicConstantBufferView(0, &shaderParameters, sizeof(ShaderParameters));

			renderContext.Dispatch(m_pAmbientOcclusionIntermediate->GetWidth(), Math::DivideAndRoundUp(m_pAmbientOcclusionIntermediate->GetHeight(), 256));
		});
}

void SSAO::SetupResources(Graphics* pGraphics)
{
	m_pAmbientOcclusionIntermediate = std::make_unique<GraphicsTexture>(pGraphics, "SSAO Blurred");
}

void SSAO::SetupPipelines(Graphics* pGraphics)
{
	// SSAO
	{
		Shader computeShader("SSAO.hlsl", ShaderType::Compute, "CSMain");

		m_pSSAORS = std::make_unique<RootSignature>();
		m_pSSAORS->FinalizeFromShader("SSAO RS", computeShader, pGraphics->GetDevice());

		m_pSSAOPSO = std::make_unique<PipelineState>();
		m_pSSAOPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pSSAOPSO->SetRootSignature(m_pSSAORS->GetRootSignature());
		m_pSSAOPSO->Finalize("SSAO PSO", pGraphics->GetDevice());
	}

	// SSAO Blur
	{
		Shader computeShader("SSAOBlur.hlsl", ShaderType::Compute, "CSMain");

		m_pSSAOBlurRS = std::make_unique<RootSignature>();
		m_pSSAOBlurRS->FinalizeFromShader("SSAO Blur RS", computeShader, pGraphics->GetDevice());

		m_pSSAOBlurPSO = std::make_unique<PipelineState>();
		m_pSSAOBlurPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pSSAOBlurPSO->SetRootSignature(m_pSSAOBlurRS->GetRootSignature());
		m_pSSAOBlurPSO->Finalize("SSAO Blur PSO", pGraphics->GetDevice());
	}
}
