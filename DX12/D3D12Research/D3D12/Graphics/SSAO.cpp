#include "stdafx.h"
#include "SSAO.h"
#include "Scene/Camera.h"
#include "Graphics/Mesh.h"
#include "Graphics/Profiler.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/GraphicsTexture.h"
#include "Graphics/Core/GraphicsBuffer.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/ResourceViews.h"

float g_AoPower = 3;
float g_AoThreshold = 0.0025f;
float g_AoRadius = 0.25f;
int g_AoSamples = 16;

SSAO::SSAO(Graphics* pGraphics) : m_pGraphics(pGraphics)
{
	if (!pGraphics->SupportsRaytracing())
	{
		SetupResources(pGraphics);
		SetupPipelines(pGraphics);
	}
}

void SSAO::OnSwapchainCreated(int widowWidth, int windowHeight)
{
	m_pAmbientOcclusionIntermediate->Create(TextureDesc::Create2D(Math::DivideAndRoundUp(widowWidth, 2), Math::DivideAndRoundUp(windowHeight, 2), DXGI_FORMAT_R8_UNORM, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess));
}

void SSAO::Execute(RGGraph& graph, const SsaoInputResources& inputResources)
{
	graph.AddPass("SSAO", [&](RGPassBuilder& builder)
		{
			builder.NeverCull();
			return [=](CommandContext& renderContext, const RGPassResource& resources)
				{
					renderContext.InsertResourceBarrier(inputResources.pDepthTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					renderContext.InsertResourceBarrier(inputResources.pNormalsTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					renderContext.InsertResourceBarrier(inputResources.pNoiseTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					renderContext.InsertResourceBarrier(inputResources.pRenderTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

					renderContext.SetComputeRootSignature(m_pSSAORS.get());
					renderContext.SetPipelineState(m_pSSAOPSO.get());

					constexpr int ssaoRandomVectors = 64;
					struct ShaderParameters
					{
						Vector4 RandomVectors[ssaoRandomVectors];
						Matrix ProjectionInverse;
						Matrix Projection;
						Matrix View;
						uint32_t Dimensions[2]{};
						float Near{ 0.0f };
						float Far{ 0.0f };
						float Power{ 0.0f };
						float Radius{ 0.0f };
						float Threshold{ 0.0f };
						int Samples{ 0 };
					} shaderParameters;

					static Vector4 randoms[ssaoRandomVectors];
					static bool written = false;
					if (!written)
					{
						srand(0);
						written = true;
						for (int i = 0; i < ssaoRandomVectors; i++)
						{
							randoms[i] = Vector4(Math::RandVector());
							randoms[i].z = Math::Lerp(0.1f, 0.8f, abs(randoms[i].z));
							randoms[i].Normalize();
							randoms[i] *= Math::Lerp(0.2f, 1.0f, (float)pow(Math::RandomRange(0.f, 1.f), 2));
						}
					}
					memcpy(shaderParameters.RandomVectors, randoms, sizeof(Vector4) * ssaoRandomVectors);

					shaderParameters.ProjectionInverse = inputResources.pCamera->GetProjectionInverse();
					shaderParameters.Projection = inputResources.pCamera->GetProjection();
					shaderParameters.View = inputResources.pCamera->GetView();
					shaderParameters.Dimensions[0] = inputResources.pRenderTarget->GetWidth();
					shaderParameters.Dimensions[1] = inputResources.pRenderTarget->GetHeight();
					shaderParameters.Near = inputResources.pCamera->GetNear();
					shaderParameters.Far = inputResources.pCamera->GetFar();
					shaderParameters.Power = g_AoPower;
					shaderParameters.Radius = g_AoRadius;
					shaderParameters.Threshold = g_AoThreshold;
					shaderParameters.Samples = g_AoSamples;

					renderContext.SetComputeDynamicConstantBufferView(0, &shaderParameters, sizeof(ShaderParameters));
					renderContext.SetDynamicDescriptor(1, 0, inputResources.pRenderTarget->GetUAV());
					renderContext.SetDynamicDescriptor(2, 0, inputResources.pDepthTexture->GetSRV());
					renderContext.SetDynamicDescriptor(2, 1, inputResources.pNormalsTexture->GetSRV());
					renderContext.SetDynamicDescriptor(2, 2, inputResources.pNoiseTexture->GetSRV());

					int dispatchGroupX = Math::DivideAndRoundUp(inputResources.pRenderTarget->GetWidth(), 16);
					int dispatchGroupY = Math::DivideAndRoundUp(inputResources.pRenderTarget->GetHeight(), 16);
					renderContext.Dispatch(dispatchGroupX, dispatchGroupY);
				};
		});

	graph.AddPass("Blur SSAO", [&](RGPassBuilder& builder)
		{
			builder.NeverCull();
			return [=](CommandContext& renderContext, const RGPassResource& resources)
				{
					renderContext.InsertResourceBarrier(m_pAmbientOcclusionIntermediate.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
					renderContext.InsertResourceBarrier(inputResources.pRenderTarget, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

					renderContext.SetComputeRootSignature(m_pSSAOBlurRS.get());
					renderContext.SetPipelineState(m_pSSAOBlurPSO.get());

					struct ShaderParameters
					{
						float Dimensions[2]{};
						uint32_t Horizontal{ 0 };
						float Far{ 0.0f };
						float Near{ 0.0f };
					} shaderParameters;

					shaderParameters.Horizontal = 1;
					shaderParameters.Dimensions[0] = 1.0f / inputResources.pRenderTarget->GetWidth();
					shaderParameters.Dimensions[1] = 1.0f / inputResources.pRenderTarget->GetHeight();
					shaderParameters.Far = inputResources.pCamera->GetFar();
					shaderParameters.Near = inputResources.pCamera->GetNear();

					renderContext.SetComputeDynamicConstantBufferView(0, &shaderParameters, sizeof(ShaderParameters));
					renderContext.SetDynamicDescriptor(1, 0, m_pAmbientOcclusionIntermediate->GetUAV());
					renderContext.SetDynamicDescriptor(2, 0, inputResources.pDepthTexture->GetSRV());
					renderContext.SetDynamicDescriptor(2, 1, inputResources.pRenderTarget->GetSRV());

					renderContext.Dispatch(Math::DivideAndRoundUp(m_pAmbientOcclusionIntermediate->GetWidth(), 256), m_pAmbientOcclusionIntermediate->GetHeight());

					renderContext.InsertResourceBarrier(m_pAmbientOcclusionIntermediate.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					renderContext.InsertResourceBarrier(inputResources.pRenderTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

					renderContext.SetDynamicDescriptor(1, 0, inputResources.pRenderTarget->GetUAV());
					renderContext.SetDynamicDescriptor(2, 0, inputResources.pDepthTexture->GetSRV());
					renderContext.SetDynamicDescriptor(2, 1, m_pAmbientOcclusionIntermediate->GetSRV());

					shaderParameters.Horizontal = 0;
					renderContext.SetComputeDynamicConstantBufferView(0, &shaderParameters, sizeof(ShaderParameters));

					renderContext.Dispatch(m_pAmbientOcclusionIntermediate->GetWidth(), Math::DivideAndRoundUp(m_pAmbientOcclusionIntermediate->GetHeight(), 256));
				};
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
		Shader computeShader("Resources/Shaders/SSAO.hlsl", Shader::Type::Compute, "CSMain");

		m_pSSAORS = std::make_unique<RootSignature>();
		m_pSSAORS->FinalizeFromShader("SSAO RS", computeShader, pGraphics->GetDevice());

		m_pSSAOPSO = std::make_unique<PipelineState>();
		m_pSSAOPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pSSAOPSO->SetRootSignature(m_pSSAORS->GetRootSignature());
		m_pSSAOPSO->Finalize("SSAO PSO", pGraphics->GetDevice());
	}

	// SSAO Blur
	{
		Shader computeShader("Resources/Shaders/SSAOBlur.hlsl", Shader::Type::Compute, "CSMain");

		m_pSSAOBlurRS = std::make_unique<RootSignature>();
		m_pSSAOBlurRS->FinalizeFromShader("SSAO Blur RS", computeShader, pGraphics->GetDevice());

		m_pSSAOBlurPSO = std::make_unique<PipelineState>();
		m_pSSAOBlurPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pSSAOBlurPSO->SetRootSignature(m_pSSAOBlurRS->GetRootSignature());
		m_pSSAOBlurPSO->Finalize("SSAO Blur PSO", pGraphics->GetDevice());
	}
}
