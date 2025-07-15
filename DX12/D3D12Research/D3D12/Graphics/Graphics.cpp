#include "stdafx.h"
#include "Graphics.h"
#include "CommandAllocatorPool.h"
#include "CommandQueue.h"
#include "CommandContext.h"
#include "OfflineDescriptorAllocator.h"
#include "DynamicResourceAllocator.h"
#include "ImGuiRenderer.h"
#include "GraphicsTexture.h"
#include "GraphicsBuffer.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "Shader.h"
#include "Mesh.h"
#include "Core/Input.h"
#include "Profiler.h"
#include "ClusteredForward.h"
#include "TiledForward.h"
#include "Scene/Camera.h"
#include "Clouds.h"
#include <DXProgrammableCapture.h>
#include "RenderGraph/RenderGraph.h"
#include "RenderGraph/Blackboard.h"
#include "RenderGraph/ResourceAllocator.h"
#include "DebugRenderer.h"
#include "ResourceViews.h"

#ifdef _DEBUG
#define D3D_VALIDATION 1
#endif

#ifndef D3D_VALIDATION
#define D3D_VALIDATION 0
#endif

#ifndef GPU_VALIDATION
#define GPU_VALIDATION 0
#endif

bool gDumpRenderGraph = true;

float g_WhitePoint = 4;
float g_MinLogLuminance = -10.0f;
float g_MaxLogLuminance = 2.0f;
float g_Tau = 10;

float g_AoPower = 3;
float g_AoThreshold = 0.0025f;
float g_AoRadius = 0.25f;
int g_AoSamples = 16;

Graphics::Graphics(uint32_t width, uint32_t height, int sampleCount):
	m_WindowWidth(width), m_WindowHeight(height), m_SampleCount(sampleCount)
{
}

Graphics::~Graphics()
{
}

void Graphics::Initialize(HWND hWnd)
{
	m_pWindow = hWnd;

	m_pCamera = std::make_unique<FreeCamera>(this);
	m_pCamera->SetPosition(Vector3(0, 100, -15));
	m_pCamera->SetRotation(Quaternion::CreateFromYawPitchRoll(Math::PIDIV4, Math::PIDIV4, 0));
	m_pCamera->SetNewPlane(400.f);
	m_pCamera->SetFarPlane(10.f);
	m_pCamera->SetViewport(0, 0, 1, 1);

	Shader::AddGlobalShaderDefine("BLOCK_SIZE", std::to_string(FORWARD_PLUS_BLOCK_SIZE));
	Shader::AddGlobalShaderDefine("SHADOWMAP_DX", std::to_string(1.0f / SHADOW_MAP_SIZE));
	Shader::AddGlobalShaderDefine("PCF_KERNEL_SIZE", std::to_string(5));
	Shader::AddGlobalShaderDefine("MAX_SHADOW_CASTERS", std::to_string(MAX_SHADOW_CASTERS));

	InitD3D();
	InitializeAssets();

	RandomizeLights(m_DesiredLightCount);
}

void Graphics::Update()
{
	PROFILE_BEGIN("UpdateGameState");

	m_pCamera->Update();

	std::sort(m_TransparentBatches.begin(), m_TransparentBatches.end(), [this](const Batch& a, const Batch& b) {
		float aDist = Vector3::DistanceSquared(a.pMesh->GetBounds().Center, m_pCamera->GetPosition());
		float bDist = Vector3::DistanceSquared(b.pMesh->GetBounds().Center, m_pCamera->GetPosition());
		return aDist > bDist;
	});

	std::sort(m_OpaqueBatches.begin(), m_OpaqueBatches.end(), [this](const Batch& a, const Batch& b) {
		float aDist = Vector3::DistanceSquared(a.pMesh->GetBounds().Center, m_pCamera->GetPosition());
		float bDist = Vector3::DistanceSquared(b.pMesh->GetBounds().Center, m_pCamera->GetPosition());
		return aDist < bDist;
	});

	// shadow map partitioning
	//////////////////////////////////
	LightData lightData;

	Matrix projection = Math::CreateOrthographicMatrix(512, 512, 10000.f, 0.1f);

	m_ShadowCasters = 0;

	lightData.LightViewProjections[m_ShadowCasters] = Matrix(XMMatrixLookAtLH(m_Lights[m_ShadowCasters].Position, Vector3::Zero, Vector3::Up)) * projection;
	lightData.ShadowMapOffsets[m_ShadowCasters] = Vector4(0.f, 0.f, 1.f, 0);
	++m_ShadowCasters;

	PROFILE_END();

	////////////////////////////////
	// Rendering Begin
	////////////////////////////////

	BeginFrame();
	m_pImGuiRenderer->Update();

	RGGraph graph(m_pResourceAllocator.get());

	struct MainData
	{
		RGResourceHandle DepthStencil;
		RGResourceHandle DepthStencilResolved;
	} sceneData{};

	sceneData.DepthStencil = graph.ImportTexture("Depth Stencil", GetDepthStencil());
	sceneData.DepthStencilResolved = graph.ImportTexture("Resolved Depth Stencil", GetResolveDepthStencil());

	uint64_t nextFenceValue = 0;
	uint64_t lightCullingFence = 0;

	// 1. depth prepass
	// - depth only pass that renders the entire scene
	// - optimization that prevents wasteful lighting calculations during the base pass
	// - required for light culling
	RGPass& prepass = graph.AddPass("Depth Prepass", [&](RGPassBuilder& builder)
		{
			builder.NeverCull();
			sceneData.DepthStencil = builder.Write(sceneData.DepthStencil);

			return [=](CommandContext& renderContext, const RGPassResource& resources)
				{
					GraphicsTexture* pDepthStencil = resources.GetTexture(sceneData.DepthStencil);
					const TextureDesc& desc = pDepthStencil->GetDesc();

					renderContext.InsertResourceBarrier(pDepthStencil, D3D12_RESOURCE_STATE_DEPTH_WRITE);
					renderContext.InsertResourceBarrier(m_pMSAANormals.get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

					RenderPassInfo info = RenderPassInfo(m_pMSAANormals.get(), RenderPassAccess::Clear_Resolve, pDepthStencil, RenderPassAccess::Clear_Store);
					info.RenderTargets[0].ResolveTarget = m_pNormals.get();

					renderContext.BeginRenderPass(info);
					renderContext.SetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
					renderContext.SetViewport(FloatRect(0, 0, (float)desc.Width, (float)desc.Height));

					renderContext.SetGraphicsPipelineState(m_pDepthPrepassPSO.get());
					renderContext.SetGraphicsRootSignature(m_pDepthPrepassRS.get());

					struct Parameters
					{
						Matrix World;
						Matrix WorldViewProj;
					} constBuffer{};

					for (const Batch& b : m_OpaqueBatches)
					{
						constBuffer.World = b.WorldMatrix;
						constBuffer.WorldViewProj = b.WorldMatrix * m_pCamera->GetViewProjection();
						
						renderContext.SetDynamicConstantBufferView(0, &constBuffer, sizeof(Parameters));
						renderContext.SetDynamicDescriptor(1, 0, b.pMaterial->pNormalTexture->GetSRV());
						b.pMesh->Draw(&renderContext);
					}

					renderContext.EndRenderPass();
				};
		});

	// 2. [OPTIONAL] depth resolve
	//  - if MSAA is enabled, run a compute shader to resolve the depth buffer
	if (m_SampleCount > 1)
	{
		graph.AddPass("Depth Resolve", [&](RGPassBuilder& builder)
			{
				builder.NeverCull();
				sceneData.DepthStencil = builder.Read(sceneData.DepthStencil);
				sceneData.DepthStencilResolved = builder.Write(sceneData.DepthStencilResolved);

				return [=](CommandContext& renderContext, const RGPassResource& resources)
				{
					GraphicsTexture* pDepthStencil = resources.GetTexture(sceneData.DepthStencil);
					GraphicsTexture* pDepthStencilResolve = resources.GetTexture(sceneData.DepthStencilResolved);

					renderContext.InsertResourceBarrier(pDepthStencilResolve, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
					renderContext.InsertResourceBarrier(pDepthStencil, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

					renderContext.SetComputeRootSignature(m_pResolveDepthRS.get());
					renderContext.SetComputePipelineState(m_pResolveDepthPSO.get());

					renderContext.SetDynamicDescriptor(0, 0, pDepthStencilResolve->GetUAV());
					renderContext.SetDynamicDescriptor(1, 0, pDepthStencil->GetSRV());

					int dispatchGroupX = Math::DivideAndRoundUp(m_WindowWidth, 16);
					int dispatchGroupY = Math::DivideAndRoundUp(m_WindowHeight, 16);
					renderContext.Dispatch(dispatchGroupX, dispatchGroupY);

					renderContext.InsertResourceBarrier(pDepthStencilResolve, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					renderContext.InsertResourceBarrier(pDepthStencil, D3D12_RESOURCE_STATE_DEPTH_READ);
					renderContext.FlushResourceBarriers();
				};
			});
	}

	graph.AddPass("SSAO", [&](RGPassBuilder& builder)
		{
			builder.NeverCull();
			sceneData.DepthStencilResolved = builder.Read(sceneData.DepthStencilResolved);
			return [=](CommandContext& renderContext, const RGPassResource& resources)
			{
				renderContext.InsertResourceBarrier(resources.GetTexture(sceneData.DepthStencilResolved), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				renderContext.InsertResourceBarrier(m_pNormals.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				renderContext.InsertResourceBarrier(m_pNoiseTexture.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				renderContext.InsertResourceBarrier(m_pSSAOTarget.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				renderContext.SetComputeRootSignature(m_pSSAORS.get());
				renderContext.SetComputePipelineState(m_pSSAOPSO.get());

				constexpr int ssaoRandomVectors = 64;
				struct ShaderParameters
				{
					Vector4 RandomVectors[ssaoRandomVectors];
					Matrix ProjectionInverse;
					Matrix Projection;
					Matrix View;
					uint32_t Dimensions[2]{};
					float Near{0.0f};
					float Far{0.0f};
					float Power{0.0f};
					float Radius{0.0f};
					float Threshold{0.0f};
					int Samples{0};
				} shaderParameters;

				static bool written = false;
				static Vector4 randoms[ssaoRandomVectors];
				if (!written)
				{
					for (int i = 0; i < ssaoRandomVectors; i++)
					{
						{
							randoms[i] = Vector4(Math::RandVector());
							randoms[i].z = Math::Lerp(0.1f, 0.8f, abs(randoms[i].z));
							randoms[i].Normalize();
							randoms[i] *= Math::Lerp(0.2f, 1.0f, (float)pow(Math::RandomRange(0, 1), 2));
						}
					}
					written = true;
				}
				memcpy(shaderParameters.RandomVectors, randoms, sizeof(Vector4) * ssaoRandomVectors);

				shaderParameters.ProjectionInverse = m_pCamera->GetProjectionInverse();
				shaderParameters.Projection = m_pCamera->GetProjection();
				shaderParameters.View = m_pCamera->GetView();
				shaderParameters.Dimensions[0] = m_pSSAOTarget->GetWidth();
				shaderParameters.Dimensions[1] = m_pSSAOTarget->GetHeight();
				shaderParameters.Near = m_pCamera->GetNear();
				shaderParameters.Far = m_pCamera->GetFar();
				shaderParameters.Power = g_AoPower;
				shaderParameters.Radius = g_AoRadius;
				shaderParameters.Threshold = g_AoThreshold;
				shaderParameters.Samples = g_AoSamples;

				renderContext.SetComputeDynamicConstantBufferView(0, &shaderParameters, sizeof(ShaderParameters));
				renderContext.SetDynamicDescriptor(1, 0, m_pSSAOTarget->GetUAV());
				renderContext.SetDynamicDescriptor(2, 0, resources.GetTexture(sceneData.DepthStencilResolved)->GetSRV());
				renderContext.SetDynamicDescriptor(2, 1, m_pNormals->GetSRV());
				renderContext.SetDynamicDescriptor(2, 2, m_pNoiseTexture->GetSRV());

				int dispatchGroupX = Math::DivideAndRoundUp(m_pSSAOTarget->GetWidth(), 16);
				int dispatchGroupY = Math::DivideAndRoundUp(m_pSSAOTarget->GetHeight(), 16);
				renderContext.Dispatch(dispatchGroupX, dispatchGroupY);

				renderContext.InsertResourceBarrier(resources.GetTexture(sceneData.DepthStencilResolved), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			};
		});

	graph.AddPass("Blur SSAO", [&](RGPassBuilder& builder)
		{
			builder.NeverCull();
			return [=](CommandContext& renderContext, const RGPassResource& resources)
			{
				renderContext.InsertResourceBarrier(m_pSSAOBlurred.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				renderContext.InsertResourceBarrier(m_pSSAOTarget.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
				
				renderContext.SetComputeRootSignature(m_pSSAOBlurRS.get());
				renderContext.SetComputePipelineState(m_pSSAOBlurPSO.get());

				struct ShaderParameters
				{
					float Dimensions[2]{};
					uint32_t Horizontal{0};
					float Far{0.0f};
					float Near{0.0f};
				} shaderParameters;

				shaderParameters.Horizontal = 1;
				shaderParameters.Dimensions[0] = 1.0f / m_pSSAOTarget->GetWidth();
				shaderParameters.Dimensions[1] = 1.0f / m_pSSAOTarget->GetHeight();
				shaderParameters.Far = m_pCamera->GetFar();
				shaderParameters.Near = m_pCamera->GetNear();
				
				GraphicsTexture* pDepth = m_SampleCount == 1 ? m_pDepthStencil.get() : m_pResolveDepthStencil.get();
				renderContext.SetComputeDynamicConstantBufferView(0, &shaderParameters, sizeof(ShaderParameters));
				renderContext.SetDynamicDescriptor(1, 0, m_pSSAOBlurred->GetUAV());
				renderContext.SetDynamicDescriptor(2, 0, pDepth->GetSRV());
				renderContext.SetDynamicDescriptor(2, 1, m_pSSAOTarget->GetSRV());

				renderContext.Dispatch(Math::DivideAndRoundUp(m_pSSAOBlurred->GetWidth(), 256), m_pSSAOBlurred->GetHeight());

				renderContext.InsertResourceBarrier(m_pSSAOBlurred.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				renderContext.InsertResourceBarrier(m_pSSAOTarget.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				renderContext.SetDynamicDescriptor(1, 0, m_pSSAOTarget->GetUAV());
				renderContext.SetDynamicDescriptor(2, 0, pDepth->GetSRV());
				renderContext.SetDynamicDescriptor(2, 1, m_pSSAOBlurred->GetSRV());

				shaderParameters.Horizontal = 0;
				renderContext.SetComputeDynamicConstantBufferView(0, &shaderParameters, sizeof(ShaderParameters));

                renderContext.Dispatch(m_pSSAOBlurred->GetWidth(), Math::DivideAndRoundUp(m_pSSAOBlurred->GetHeight(), 256));
			};
		});

	// 4. shadow mapping
	//  - renders the scene depth onto a separate depth buffer from the light's view
	if (m_ShadowCasters > 0)
	{
		graph.AddPass("Shadows", [&](RGPassBuilder& builder)
			{
				builder.NeverCull();

				return[=](CommandContext& context, const RGPassResource& resources)
					{
						context.InsertResourceBarrier(m_pShadowMap.get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);

						context.BeginRenderPass(RenderPassInfo(m_pShadowMap.get(), RenderPassAccess::Clear_Store));

						context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

						for (int i = 0; i < m_ShadowCasters; ++i)
						{
							GPU_PROFILE_SCOPE("Light View", &context);
							const Vector4& shadowOffset = lightData.ShadowMapOffsets[i];
							FloatRect viewport;
							viewport.Left = shadowOffset.x * (float)m_pShadowMap->GetWidth();
							viewport.Top = shadowOffset.y * (float)m_pShadowMap->GetHeight();
							viewport.Right = viewport.Left + shadowOffset.z * (float)m_pShadowMap->GetWidth();
							viewport.Bottom = viewport.Top + shadowOffset.z * (float)m_pShadowMap->GetHeight();
							context.SetViewport(viewport);

							struct PerObjectData
							{
								Matrix WorldViewProjection;
							} ObjectData{};

							{
								GPU_PROFILE_SCOPE("Opaque", &context);
								context.SetGraphicsPipelineState(m_pShadowPSO.get());
								context.SetGraphicsRootSignature(m_pShadowRS.get());

								for (const Batch& b : m_OpaqueBatches)
								{
									ObjectData.WorldViewProjection = b.WorldMatrix * lightData.LightViewProjections[i];
									
									context.SetDynamicConstantBufferView(0, &ObjectData, sizeof(ObjectData));
									b.pMesh->Draw(&context);
								}
							}

							{
								GPU_PROFILE_SCOPE("Transparent", &context);
								context.SetGraphicsPipelineState(m_pShadowAlphaPSO.get());
								context.SetGraphicsRootSignature(m_pShadowRS.get());

								context.SetDynamicConstantBufferView(0, &ObjectData, sizeof(ObjectData));
								for (const Batch& b : m_TransparentBatches)
								{
									ObjectData.WorldViewProjection = b.WorldMatrix * lightData.LightViewProjections[i];
									
									context.SetDynamicConstantBufferView(0, &ObjectData, sizeof(ObjectData));
									context.SetDynamicDescriptor(1, 0, b.pMaterial->pDiffuseTexture->GetSRV());
									b.pMesh->Draw(&context);
								}
							}
						}
						context.EndRenderPass();
					};
			});
	}

	if (m_RenderPath == RenderPath::Tiled)
	{
		TiledForwardInputResource resources;
		resources.DepthBuffer = sceneData.DepthStencil;
		resources.ResolvedDepthBuffer = sceneData.DepthStencilResolved;
		resources.pOpaqueBatches = &m_OpaqueBatches;
		resources.pTransparentBatches = &m_TransparentBatches;
		resources.pRenderTarget = GetCurrentRenderTarget();
		resources.pLightBuffer = m_pLightBuffer.get();
		resources.pCamera = m_pCamera.get();
		resources.pShadowMap = m_pShadowMap.get();
		resources.pLightData = &lightData;
		m_pTiledForward->Execute(graph, resources);
	}
	else if (m_RenderPath == RenderPath::Clustered)
	{
		ClusteredForwardInputResource resources;
		resources.DepthBuffer = sceneData.DepthStencil;
		resources.pOpaqueBatches = &m_OpaqueBatches;
		resources.pTransparentBatches = &m_TransparentBatches;
		resources.pRenderTarget = GetCurrentRenderTarget();
		resources.pLightBuffer = m_pLightBuffer.get();
		resources.pCamera = m_pCamera.get();
		resources.pShadowMap = m_pShadowMap.get();
		resources.pLightData = &lightData;
		resources.pAO = m_pSSAOTarget.get();
		m_pClusteredForward->Execute(graph, resources);
	}

	m_pDebugRenderer->Render(graph);

	// 7. MSAA render target resolve
	//  - we have to resolve a MSAA render target ourselves.
	if (m_SampleCount > 1)
	{
		graph.AddPass("Resolve MSAA", [&](RGPassBuilder& builder)
			{
				builder.NeverCull();

				return [=](CommandContext& context, const RGPassResource& resources)
					{
						context.InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
						context.InsertResourceBarrier(m_pHDRRenderTarget.get(), D3D12_RESOURCE_STATE_RESOLVE_DEST);
						context.ResolveResource(GetCurrentRenderTarget(), 0, m_pHDRRenderTarget.get(), 0, RENDER_TARGET_FORMAT);
					};
			});
	}
		

	// 8. tonemap
	{
        bool downscaleTonemap = true;
		GraphicsTexture* pTonemapInput = downscaleTonemap ? m_pDownscaledColor.get() : m_pHDRRenderTarget.get();
		RGResourceHandle toneMappingInput = graph.ImportTexture("Tonemap Input", pTonemapInput);

		if (downscaleTonemap)
		{
			graph.AddPass("Downsample Color", [&](RGPassBuilder& builder)
				{
					builder.NeverCull();
					toneMappingInput = builder.Write(toneMappingInput);

					return[=](CommandContext& context, const RGPassResource& resources)
						{
							GraphicsTexture* pTonemapInput = resources.GetTexture(toneMappingInput);
							context.InsertResourceBarrier(pTonemapInput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
							context.InsertResourceBarrier(m_pHDRRenderTarget.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

							context.SetComputePipelineState(m_pGenerateMipsPSO.get());
							context.SetComputeRootSignature(m_pGenerateMipsRS.get());

							struct DownscaleParameters
							{
								uint32_t TargetDimensions[2];
							} Parameters{};

							Parameters.TargetDimensions[0] = pTonemapInput->GetWidth();
							Parameters.TargetDimensions[1] = pTonemapInput->GetHeight();

							context.SetComputeDynamicConstantBufferView(0, &Parameters, sizeof(DownscaleParameters));
							context.SetDynamicDescriptor(1, 0, pTonemapInput->GetUAV());
							context.SetDynamicDescriptor(2, 0, m_pHDRRenderTarget->GetSRV());

							context.Dispatch(Math::DivideAndRoundUp(pTonemapInput->GetWidth(), 16), Math::DivideAndRoundUp(pTonemapInput->GetHeight(), 16), 1);
						};
				});
		}

		// exposure adjustment
		// luminance histogram, collect the pixel count into a 256 bins histogram with luminance
		graph.AddPass("Luminance Histogram", [&](RGPassBuilder& builder)
			{
				builder.NeverCull();
				toneMappingInput = builder.Read(toneMappingInput);

				return[=](CommandContext& context, const RGPassResource& resources)
					{
						GraphicsTexture* pTonemapInput = resources.GetTexture(toneMappingInput);

						context.InsertResourceBarrier(m_pLuminanceHistogram.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
						context.InsertResourceBarrier(pTonemapInput, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

						context.ClearUavUInt(m_pLuminanceHistogram.get(), m_pLuminanceHistogram->GetUAV());

						context.SetComputePipelineState(m_pLuminanceHistogramPSO.get());
						context.SetComputeRootSignature(m_pLuminanceHistogramRS.get());

						struct HistogramParameters
						{
							uint32_t Width;
							uint32_t Height;
							float MinLogLuminance;
							float OneOverLogLuminanceRange;
						} Parameters;

						Parameters.Width = pTonemapInput->GetWidth();
						Parameters.Height = pTonemapInput->GetHeight();
						Parameters.MinLogLuminance = g_MinLogLuminance;
						Parameters.OneOverLogLuminanceRange = 1.0f / (g_MaxLogLuminance - g_MinLogLuminance);

						context.SetComputeDynamicConstantBufferView(0, &Parameters, sizeof(HistogramParameters));
						context.SetDynamicDescriptor(1, 0, m_pLuminanceHistogram->GetUAV());
						context.SetDynamicDescriptor(2, 0, pTonemapInput->GetSRV());

						context.Dispatch(Math::DivideAndRoundUp(pTonemapInput->GetWidth(), 16), Math::DivideAndRoundUp(pTonemapInput->GetHeight(), 16));
					};
			});

		// average luminance, compute the average luminance from the histogram
		graph.AddPass("Average Luminance", [&](RGPassBuilder& builder)
			{
				builder.NeverCull();

				return[=](CommandContext& context, const RGPassResource& resources)
					{
						context.InsertResourceBarrier(m_pLuminanceHistogram.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
						context.InsertResourceBarrier(m_pAverageLuminance.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

						context.SetComputePipelineState(m_pAverageLuminancePSO.get());
						context.SetComputeRootSignature(m_pAverageLuminanceRS.get());

						struct AverageLuminanceParameters
						{
							uint32_t PixelCount;
							float MinLogLuminance;
							float LogLuminanceRange;
							float TimeDelta;
							float Tau;
						} AverageParameters;

						AverageParameters.PixelCount = pTonemapInput->GetWidth() * pTonemapInput->GetHeight();
						AverageParameters.MinLogLuminance = g_MinLogLuminance;
						AverageParameters.LogLuminanceRange = g_MaxLogLuminance - g_MinLogLuminance;
						AverageParameters.TimeDelta = GameTimer::DeltaTime();
						AverageParameters.Tau = g_Tau;

						context.SetComputeDynamicConstantBufferView(0, &AverageParameters, sizeof(AverageLuminanceParameters));
						context.SetDynamicDescriptor(1, 0, m_pAverageLuminance->GetUAV());
						context.SetDynamicDescriptor(2, 0, m_pLuminanceHistogram->GetSRV());

						context.Dispatch(1, 1, 1);
					};
			});

		graph.AddPass("Tonemap", [&](RGPassBuilder& builder)
			{
				builder.NeverCull();

				return[=](CommandContext& context, const RGPassResource& resources)
					{

						context.InsertResourceBarrier(GetCurrentBackbuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET);
						context.InsertResourceBarrier(m_pAverageLuminance.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
						context.InsertResourceBarrier(m_pHDRRenderTarget.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

						context.SetGraphicsPipelineState(m_pToneMapPSO.get());
						context.SetGraphicsRootSignature(m_pToneMapRS.get());
						context.SetViewport(FloatRect(0, 0, (float)m_WindowWidth, (float)m_WindowHeight));
						context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
						context.BeginRenderPass(RenderPassInfo(GetCurrentBackbuffer(), RenderPassAccess::Clear_Store, nullptr, RenderPassAccess::NoAccess));

						context.SetDynamicConstantBufferView(0, &g_WhitePoint, sizeof(float));
						context.SetDynamicDescriptor(1, 0, m_pHDRRenderTarget->GetSRV());
						context.SetDynamicDescriptor(1, 1, m_pAverageLuminance->GetSRV());
						context.Draw(0, 3);
						context.EndRenderPass();
					};
			});
	}

	// 9. UI
	//  - ImGui render, pretty straight forward
	{
		//ImGui::ShowDemoWindow();
		//m_pClouds->RenderUI();
		m_pImGuiRenderer->Render(graph, GetCurrentBackbuffer());
	}

	graph.AddPass("Temp Barriers", [&](RGPassBuilder& builder)
		{
			builder.NeverCull();
			return[=](CommandContext& context, const RGPassResource& resources)
			{
				context.InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET);
				context.InsertResourceBarrier(GetCurrentBackbuffer(), D3D12_RESOURCE_STATE_PRESENT);
			};
		});

	graph.Compile();
	if (gDumpRenderGraph)
	{
		graph.DumpGraphMermaid("graph.html");
		gDumpRenderGraph = false;
	}
	nextFenceValue = graph.Execute(this);
	

	/*m_pClouds->Render(m_pResolvedRenderTarget.get(), m_pResolveDepthStencil.get());

	{
		CommandContext* pCommandContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
		Profiler::Instance()->Begin("Blit to Backbuffer", pCommandContext);
		pCommandContext->InsertResourceBarrier(m_pResolvedRenderTarget.get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
		pCommandContext->InsertResourceBarrier(GetCurrentBackbuffer(), D3D12_RESOURCE_STATE_COPY_DEST);
		pCommandContext->FlushResourceBarriers();
		pCommandContext->CopyResource(m_pResolvedRenderTarget.get(), GetCurrentBackbuffer());
		pCommandContext->InsertResourceBarrier(GetCurrentBackbuffer(), D3D12_RESOURCE_STATE_PRESENT);
		Profiler::Instance()->End(pCommandContext);

		nextFenceValue = pCommandContext->Execute(false);
	}*/

	// 10. present
	//  - set fence for the currently queued frame
	//  - present the frame buffer
	//  - wait for the next frame to be finished to start queueing work for it
	EndFrame(nextFenceValue);
}

void Graphics::Shutdown()
{
	// wait for all the GPU work to finish
	IdleGPU();
	m_pSwapchain->SetFullscreenState(false, nullptr);
}

void Graphics::BeginFrame()
{
	m_pImGuiRenderer->NewFrame();
}

void Graphics::EndFrame(uint64_t fenceValue)
{
	// the top third(triple buffer) is not need wait, just record the every frame fenceValue.
	// the 'm_CurrentBackBufferIndex' is always in the new buffer frame
	// we present and request the new backbuffer index and wait for that one to finish on the GPU before starting to queue work for that frame

	Profiler::Instance()->BeginReadback(m_Frame);
	m_FenceValues[m_CurrentBackBufferIndex] = fenceValue;
	m_pSwapchain->Present(1, 0);
	m_CurrentBackBufferIndex = m_pSwapchain->GetCurrentBackBufferIndex();
	WaitForFence(m_FenceValues[m_CurrentBackBufferIndex]);
	Profiler::Instance()->EndReadBack(m_Frame);
	++m_Frame;

	m_pDebugRenderer->EndFrame();
}

void Graphics::InitD3D()
{
	E_LOG(LogType::Info, "Graphics::InitD3D");
	UINT dxgiFactoryFlags = 0;

#if D3D_VALIDATION
	// enable debug layer
	ComPtr<ID3D12Debug> pDebugController;
	HR(D3D12GetDebugInterface(IID_PPV_ARGS(&pDebugController)));
	pDebugController->EnableDebugLayer();  // CPU-side

#if GPU_VALIDATION
	ComPtr<ID3D12Debug1> pDebugController1;
	HR(D3D12GetDebugInterface(IID_PPV_ARGS(&pDebugController1)));
	pDebugController1->SetEnableGPUBasedValidation(true);  // GPU-side
#endif

	// additional DXGI debug layers
	dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

	// factory
	HR(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_pFactory)));

	// look for an adapter
	uint32_t adapter = 0;
	ComPtr<IDXGIAdapter4> pAdapter;
	while (m_pFactory->EnumAdapterByGpuPreference(adapter++, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(pAdapter.ReleaseAndGetAddressOf())) != DXGI_ERROR_NOT_FOUND)
	{
		DXGI_ADAPTER_DESC3 desc;
		pAdapter->GetDesc3(&desc);

		if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
		{
			char name[256];
			ToMultibyte(desc.Description, name, 256);
			E_LOG(LogType::Info, "Using %s", name);
			break;
		}
	}

	// device
	HR(D3D12CreateDevice(pAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_pDevice)));
	pAdapter.Reset();

#if D3D_VALIDATION
	ID3D12InfoQueue* pInfoQueue = nullptr;
	if (HR(m_pDevice->QueryInterface(IID_PPV_ARGS(&pInfoQueue))))
	{
		// Suppress whole categories of messages
 		//D3D12_MESSAGE_CATEGORY Categories[] = {};
 
 		// Suppress messages based on their severity level
 		D3D12_MESSAGE_SEVERITY Severities[] =
 		{
 			D3D12_MESSAGE_SEVERITY_INFO
 		};
 
 		// Suppress individual messages by their ID
 		D3D12_MESSAGE_ID DenyIds[] =
 		{
 			// This occurs when there are uninitialized descriptors in a descriptor table, even when a
 			// shader does not access the missing descriptors.  I find this is common when switching
 			// shader permutations and not wanting to change much code to reorder resources.
 			D3D12_MESSAGE_ID_INVALID_DESCRIPTOR_HANDLE,
 		};
 
 		D3D12_INFO_QUEUE_FILTER NewFilter = {};
 		//NewFilter.DenyList.NumCategories = _countof(Categories);
 		//NewFilter.DenyList.pCategoryList = Categories;
 		NewFilter.DenyList.NumSeverities = _countof(Severities);
 		NewFilter.DenyList.pSeverityList = Severities;
 		NewFilter.DenyList.NumIDs = _countof(DenyIds);
 		NewFilter.DenyList.pIDList = DenyIds;
 
 #if 1
		HR(pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true));
 #endif

 		pInfoQueue->PushStorageFilter(&NewFilter);
 		pInfoQueue->Release();
	}
#endif

	D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
	if (m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options)) == S_OK)
	{
		m_RenderPassTier = options.RenderPassesTier;
		m_RayTracingTier = options.RaytracingTier;
	}

	// check msaa support
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qualityLevels;
	qualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	qualityLevels.Format = RENDER_TARGET_FORMAT;
	qualityLevels.NumQualityLevels = 0;
	qualityLevels.SampleCount = m_SampleCount;
	HR(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &qualityLevels, sizeof(qualityLevels)));
	m_SampleQuality = qualityLevels.NumQualityLevels - 1;

	// create all the required command queues
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_COMPUTE] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_COMPUTE);
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_COPY] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_COPY);
	//m_CommandQueues[D3D12_COMMAND_LIST_TYPE_BUNDLE] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_BUNDLE);

	// allocate descriptor heaps pool
	assert(m_DescriptorHeaps.size() == D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES);
	m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV] = std::make_unique<OfflineDescriptorAllocator>(this, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 256);
	m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER] = std::make_unique<OfflineDescriptorAllocator>(this, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 128);
	m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_RTV] = std::make_unique<OfflineDescriptorAllocator>(this, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 128);
	m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_DSV] = std::make_unique<OfflineDescriptorAllocator>(this, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 64);

	m_pDynamicAllocationManager = std::make_unique<DynamicAllocationManager>(this);
	Profiler::Instance()->Initialize(this);

	// swap chain
	CreateSwapchain();

	// create the textures but don't create the resources themselves yet. 
	for (int i = 0; i < FRAME_COUNT; i++)
	{
		m_Backbuffers[i] = std::make_unique<GraphicsTexture>(this, "Render Target");
	}
	m_pDepthStencil = std::make_unique<GraphicsTexture>(this, "Depth Stencil");

	if (m_SampleCount > 1)
	{
		m_pResolveDepthStencil = std::make_unique<GraphicsTexture>(this, "Resolved Depth Stencil");
		m_pMultiSampleRenderTarget = std::make_unique<GraphicsTexture>(this, "MSAA Render Target");
		m_pResolvedRenderTarget = std::make_unique<GraphicsTexture>(this, "Resolved Render Target");
	}
	
	m_pHDRRenderTarget = std::make_unique<GraphicsTexture>(this, "HDR Render Target");
	m_pDownscaledColor = std::make_unique<GraphicsTexture>(this, "Downscaled HDR Target");
	m_pMSAANormals = std::make_unique<GraphicsTexture>(this, "MSAA Normals");
	m_pNormals = std::make_unique<GraphicsTexture>(this, "Normals");
	m_pSSAOTarget = std::make_unique<GraphicsTexture>(this, "SSAO Target");
	m_pSSAOBlurred = std::make_unique<GraphicsTexture>(this, "SSAO Blurred");

	m_pClusteredForward = std::make_unique<ClusteredForward>(this);
	m_pTiledForward = std::make_unique<TiledForward>(this);
	
	m_pImGuiRenderer = std::make_unique<ImGuiRenderer>(this);
	m_pImGuiRenderer->AddUpdateCallback(ImGuiCallbackDelegate::CreateRaw(this, &Graphics::UpdateImGui));

	m_pResourceAllocator = std::make_unique<RGResourceAllocator>(this);

	m_pClouds = std::make_unique<Clouds>(this);
	m_pClouds->Initialize();

	OnResize(m_WindowWidth, m_WindowHeight);

	m_pDebugRenderer = std::make_unique<DebugRenderer>(this);
	m_pDebugRenderer->SetCamera(m_pCamera.get());
}

void Graphics::CreateSwapchain()
{
	m_pSwapchain.Reset();

	DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
	swapchainDesc.Width = m_WindowWidth;
	swapchainDesc.Height = m_WindowHeight;
	swapchainDesc.Format = SWAPCHAIN_FORMAT;
	swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapchainDesc.BufferCount = FRAME_COUNT;
	swapchainDesc.Scaling = DXGI_SCALING_NONE;
	swapchainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	swapchainDesc.SampleDesc.Count = 1;  // must set for msaa >= 1, not 0
	swapchainDesc.SampleDesc.Quality = 0;
	swapchainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	swapchainDesc.Stereo = false;

	ComPtr<IDXGISwapChain1> pSwapChain = nullptr;

	DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsDesc{};
	fsDesc.RefreshRate.Denominator = 60;
	fsDesc.RefreshRate.Numerator = 1;
	fsDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	fsDesc.Windowed = true;

	HR(m_pFactory->CreateSwapChainForHwnd(
		m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT]->GetCommandQueue(),
		m_pWindow,
		&swapchainDesc,
		&fsDesc,
		nullptr,
		pSwapChain.GetAddressOf()));

	pSwapChain.As(&m_pSwapchain);
}

void Graphics::OnResize(int width, int height)
{
	E_LOG(LogType::Info, "Viewport resized: %dx%d", width, height);
	m_WindowWidth = width;
	m_WindowHeight = height;

	IdleGPU();
	
	for (int i = 0; i < FRAME_COUNT; i++)
	{
		m_Backbuffers[i]->Release();
	}
	m_pDepthStencil->Release();

	// resize the buffers
	HR(m_pSwapchain->ResizeBuffers(
			FRAME_COUNT,
			m_WindowWidth,
			m_WindowHeight,
			SWAPCHAIN_FORMAT,
			DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

	m_CurrentBackBufferIndex = 0;

	// recreate the render target views
	for (int i = 0; i < FRAME_COUNT; i++)
	{
		ID3D12Resource* pResource = nullptr;
 		HR(m_pSwapchain->GetBuffer(i, IID_PPV_ARGS(&pResource)));
		m_Backbuffers[i]->CreateForSwapChain(pResource);
	}

	m_pDepthStencil->Create(TextureDesc::CreateDepth(m_WindowWidth, m_WindowHeight, DEPTH_STENCIL_FORMAT, TextureFlag::DepthStencil | TextureFlag::ShaderResource, m_SampleCount, ClearBinding(0.0f, 0)));
	if (m_SampleCount > 1)
	{
		m_pResolveDepthStencil->Create(TextureDesc::Create2D(m_WindowWidth, m_WindowHeight, DXGI_FORMAT_R32_FLOAT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess));
		m_pResolvedRenderTarget->Create(TextureDesc::CreateRenderTarget(width, height, RENDER_TARGET_FORMAT, TextureFlag::RenderTarget | TextureFlag::ShaderResource, 1, ClearBinding(Color(0,0,0,0))));

		m_pMultiSampleRenderTarget->Create(TextureDesc::CreateRenderTarget(width, height, RENDER_TARGET_FORMAT, TextureFlag::RenderTarget, m_SampleCount, ClearBinding(Color(0,0,0,0))));
	}

	m_pHDRRenderTarget->Create(TextureDesc::CreateRenderTarget(width, height, RENDER_TARGET_FORMAT, TextureFlag::ShaderResource | TextureFlag::RenderTarget));
	m_pDownscaledColor->Create(TextureDesc::Create2D(Math::DivideAndRoundUp(width, 4), Math::DivideAndRoundUp(height, 4), RENDER_TARGET_FORMAT, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource));

	m_pMSAANormals->Create(TextureDesc::CreateRenderTarget(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, TextureFlag::RenderTarget, m_SampleCount));
	m_pNormals->Create(TextureDesc::Create2D(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, TextureFlag::ShaderResource));
	m_pSSAOTarget->Create(TextureDesc::Create2D(Math::DivideAndRoundUp(width, 2), Math::DivideAndRoundUp(height, 2), DXGI_FORMAT_R8_UNORM, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess));
	m_pSSAOBlurred->Create(TextureDesc::Create2D(Math::DivideAndRoundUp(width, 2), Math::DivideAndRoundUp(height, 2), DXGI_FORMAT_R8_UNORM, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess));

	m_pCamera->SetDirty();

	m_pClusteredForward->OnSwapchainCreated(width, height);
	m_pTiledForward->OnSwapchainCreated(width, height);
	m_pClouds->OnSwapchainCreated(width, height);
}

void Graphics::InitializeAssets()
{
	m_pLightBuffer = std::make_unique<Buffer>(this, "Light Buffer");

	// input layout
	// universal
	D3D12_INPUT_ELEMENT_DESC inputElements[] = {
			D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			D3D12_INPUT_ELEMENT_DESC{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			D3D12_INPUT_ELEMENT_DESC{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	D3D12_INPUT_ELEMENT_DESC depthOnlyInputElements[] = {
			D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	// shadow mapping
	// vertex shader-only pass that writes to the depth buffer using the light matrix
	{
		
		// opaque
		Shader vertexShader("Resources/Shaders/DepthOnly.hlsl", Shader::Type::VertexShader, "VSMain");
			
		// root signature
		m_pShadowRS = std::make_unique<RootSignature>();
		m_pShadowRS->FinalizeFromShader("Shadow Mapping RS", vertexShader, m_pDevice.Get());

		// pipeline state
		m_pShadowPSO = std::make_unique<GraphicsPipelineState>();
		m_pShadowPSO->SetInputLayout(depthOnlyInputElements, sizeof(depthOnlyInputElements) / sizeof(depthOnlyInputElements[0]));
		m_pShadowPSO->SetRootSignature(m_pShadowRS->GetRootSignature());
		m_pShadowPSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pShadowPSO->SetRenderTargetFormats(nullptr, 0, DEPTH_STENCIL_SHADOW_FORMAT, 1, 0);
		m_pShadowPSO->SetCullMode(D3D12_CULL_MODE_NONE);
		m_pShadowPSO->SetDepthBias(-1, -5.f, -4.f);
		m_pShadowPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		m_pShadowPSO->Finalize("Shadow Mapping (Opaque) Pipeline", m_pDevice.Get());
		
		// transparent
		Shader alphaVertexShader("Resources/Shaders/DepthOnly.hlsl", Shader::Type::VertexShader, "VSMain", { "ALPHA_BLEND" });
		Shader alphaPixelShader("Resources/Shaders/DepthOnly.hlsl", Shader::Type::PixelShader, "PSMain", { "ALPHA_BLEND" });
			
		m_pShadowAlphaPSO = std::make_unique<GraphicsPipelineState>(*m_pShadowPSO);
		m_pShadowAlphaPSO->SetVertexShader(alphaVertexShader.GetByteCode(), alphaVertexShader.GetByteCodeSize());
		m_pShadowAlphaPSO->SetPixelShader(alphaPixelShader.GetByteCode(), alphaPixelShader.GetByteCodeSize());
		m_pShadowAlphaPSO->Finalize("Shadow Mapping (Alpha) Pipeline", m_pDevice.Get());
		
		m_pShadowMap = std::make_unique<GraphicsTexture>(this, "Shadow Map");
		m_pShadowMap->Create(TextureDesc(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, DEPTH_STENCIL_SHADOW_FORMAT, TextureFlag::DepthStencil | TextureFlag::ShaderResource, 1, ClearBinding(0.0f, 0)));
	}

	// depth prepass
	// simple vertex shader to fill the depth buffer to optimize later passes
	{
		Shader vertexShader("Resources/Shaders/Prepass.hlsl", Shader::Type::VertexShader, "VSMain");
		Shader pixelShader("Resources/Shaders/Prepass.hlsl", Shader::Type::PixelShader, "PSMain");

		// root signature
		m_pDepthPrepassRS = std::make_unique<RootSignature>();
		m_pDepthPrepassRS->FinalizeFromShader("Depth Prepass RS", vertexShader, m_pDevice.Get());

		// pipeline state
		m_pDepthPrepassPSO = std::make_unique<GraphicsPipelineState>();
		m_pDepthPrepassPSO->SetInputLayout(inputElements, sizeof(inputElements) / sizeof(inputElements[0]));
		m_pDepthPrepassPSO->SetRootSignature(m_pDepthPrepassRS->GetRootSignature());
		m_pDepthPrepassPSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pDepthPrepassPSO->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
		m_pDepthPrepassPSO->SetRenderTargetFormat(DXGI_FORMAT_R32G32B32A32_FLOAT, DEPTH_STENCIL_FORMAT, m_SampleCount, m_SampleQuality);
		m_pDepthPrepassPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		m_pDepthPrepassPSO->Finalize("Depth Prepass Pipeline", m_pDevice.Get());
	}

	// luminance histogram
	{
		Shader computeShader("Resources/Shaders/LuminanceHistogram.hlsl", Shader::Type::ComputeShader, "CSMain");

		// root signature
		m_pLuminanceHistogramRS = std::make_unique<RootSignature>();
		m_pLuminanceHistogramRS->FinalizeFromShader("Luminance Histogram RS", computeShader, m_pDevice.Get());

		// pipeline state
		m_pLuminanceHistogramPSO = std::make_unique<ComputePipelineState>();
		m_pLuminanceHistogramPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pLuminanceHistogramPSO->SetRootSignature(m_pLuminanceHistogramRS->GetRootSignature());
		m_pLuminanceHistogramPSO->Finalize("Luminance Histogram PSO", m_pDevice.Get());

		m_pLuminanceHistogram = std::make_unique<Buffer>(this, "Luminance Histogram");
		m_pLuminanceHistogram->Create(BufferDesc::CreateByteAddress(sizeof(uint32_t) * 256));
		m_pAverageLuminance = std::make_unique<GraphicsTexture>(this, "Average Luminance");
		m_pAverageLuminance->Create(TextureDesc::Create2D(1, 1, DXGI_FORMAT_R32_FLOAT, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource));
	}

	// average luminance
	{
		Shader computeShader("Resources/Shaders/AverageLuminance.hlsl", Shader::Type::ComputeShader, "CSMain");

		// root signature
		m_pAverageLuminanceRS = std::make_unique<RootSignature>();
		m_pAverageLuminanceRS->FinalizeFromShader("Average Luminance RS", computeShader, m_pDevice.Get());

		// pipeline state
		m_pAverageLuminancePSO = std::make_unique<ComputePipelineState>();
		m_pAverageLuminancePSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pAverageLuminancePSO->SetRootSignature(m_pAverageLuminanceRS->GetRootSignature());
		m_pAverageLuminancePSO->Finalize("Average Luminance PSO", m_pDevice.Get());
	}

	// tonemapping
	{
		Shader vertexShader("Resources/Shaders/Tonemapping.hlsl", Shader::Type::VertexShader, "VSMain");
		Shader pixelShader("Resources/Shaders/Tonemapping.hlsl", Shader::Type::PixelShader, "PSMain");

		// rootSignature
		m_pToneMapRS = std::make_unique<RootSignature>();
		m_pToneMapRS->FinalizeFromShader("Tonemapping RS", vertexShader, m_pDevice.Get());

		// pipeline state
		m_pToneMapPSO = std::make_unique<GraphicsPipelineState>();
		m_pToneMapPSO->SetDepthEnable(false);
		m_pToneMapPSO->SetDepthWrite(false);
		m_pToneMapPSO->SetRootSignature(m_pToneMapRS->GetRootSignature());
		m_pToneMapPSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pToneMapPSO->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
		m_pToneMapPSO->SetRenderTargetFormat(SWAPCHAIN_FORMAT, DEPTH_STENCIL_FORMAT, 1, 0);
		m_pToneMapPSO->Finalize("Tonemapping Pipeline", m_pDevice.Get());
	}

	// depth resolve
	// resolves a multisampled buffer to a normal depth buffer
	// only required when the sample count > 1
	if (m_SampleCount > 1)
	{
		Shader computeShader("Resources/Shaders/ResolveDepth.hlsl", Shader::Type::ComputeShader, "CSMain");

		m_pResolveDepthRS = std::make_unique<RootSignature>();
		m_pResolveDepthRS->FinalizeFromShader("Resolve Depth RS", computeShader, m_pDevice.Get());

		m_pResolveDepthPSO = std::make_unique<ComputePipelineState>();
		m_pResolveDepthPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pResolveDepthPSO->SetRootSignature(m_pResolveDepthRS->GetRootSignature());
		m_pResolveDepthPSO->Finalize("Resolve Depth Pipeline", m_pDevice.Get());
	}

	// mip generation
	{
		Shader computeShader("Resources/Shaders/GenerateMips.hlsl", Shader::Type::ComputeShader, "CSMain");

		m_pGenerateMipsRS = std::make_unique<RootSignature>();
		m_pGenerateMipsRS->FinalizeFromShader("Generate Mips RS", computeShader, m_pDevice.Get());

		m_pGenerateMipsPSO = std::make_unique<ComputePipelineState>();
		m_pGenerateMipsPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pGenerateMipsPSO->SetRootSignature(m_pGenerateMipsRS->GetRootSignature());
		m_pGenerateMipsPSO->Finalize("Generate Mips PSO", m_pDevice.Get());
	}

	// SSAO
	{
		Shader computeShader("Resources/Shaders/SSAO.hlsl", Shader::Type::ComputeShader, "CSMain");

		m_pSSAORS = std::make_unique<RootSignature>();
		m_pSSAORS->FinalizeFromShader("SSAO RS", computeShader, m_pDevice.Get());

		m_pSSAOPSO = std::make_unique<ComputePipelineState>();
		m_pSSAOPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pSSAOPSO->SetRootSignature(m_pSSAORS->GetRootSignature());
		m_pSSAOPSO->Finalize("SSAO PSO", m_pDevice.Get());
	}

	// SSAO Blur
	{
		Shader computeShader("Resources/Shaders/SSAOBlur.hlsl", Shader::Type::ComputeShader, "CSMain");

		m_pSSAOBlurRS = std::make_unique<RootSignature>();
		m_pSSAOBlurRS->FinalizeFromShader("SSAO Blur RS", computeShader, m_pDevice.Get());

		m_pSSAOBlurPSO = std::make_unique<ComputePipelineState>();
		m_pSSAOBlurPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pSSAOBlurPSO->SetRootSignature(m_pSSAOBlurRS->GetRootSignature());
		m_pSSAOBlurPSO->Finalize("SSAO Blur PSO", m_pDevice.Get());
	}

	CommandContext* pCommandContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COPY);
	// geometry
	{
		m_pMesh = std::make_unique<Mesh>();
		m_pMesh->Load("Resources/sponza/sponza.dae", this, pCommandContext);

		for (int i = 0; i < m_pMesh->GetMeshCount(); i++)
		{
			Batch b;
			b.Bounds = m_pMesh->GetMesh(i)->GetBounds();
			b.pMesh = m_pMesh->GetMesh(i);
			b.pMaterial = &m_pMesh->GetMaterial(b.pMesh->GetMaterialId());
			b.WorldMatrix = Matrix::Identity;
			if (b.pMaterial->IsTransparent)
			{
				m_TransparentBatches.push_back(b);
			}
			else
			{
				m_OpaqueBatches.push_back(b);
			}
		}
	}

	ComPtr<ID3D12Device5> pDevice;
	if (m_RayTracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED && m_pDevice.As(&pDevice) == S_OK)
	{
		PIX_CAPTURE_SCOPE();

		CommandContext* pContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
		ID3D12GraphicsCommandList* pCommandList = pContext->GetCommandList();
		ComPtr<ID3D12GraphicsCommandList4> pCmd;
		pCommandList->QueryInterface(IID_PPV_ARGS(pCmd.GetAddressOf()));

		std::unique_ptr<Buffer> pBLAS, pTLAS;
		std::unique_ptr<Buffer> pBLASScratch;
		std::unique_ptr<Buffer> pTLASScratch, pDescriptorsBuffer;

		ComPtr<ID3D12StateObject> pPipeline;
		ComPtr<ID3D12StateObjectProperties> pPipelineProperties;

		std::unique_ptr<RootSignature> pRayGenSignature = std::make_unique<RootSignature>();
		std::unique_ptr<RootSignature> pHitSignature = std::make_unique<RootSignature>();
		std::unique_ptr<RootSignature> pMissSignature = std::make_unique<RootSignature>();
		std::unique_ptr<RootSignature> pDummySignature = std::make_unique<RootSignature>();

		std::unique_ptr<GraphicsTexture> pOutputTexture = std::make_unique<GraphicsTexture>(this, "Ray Tracing Output");
		UnorderedAccessView* pOutputRawUAV = nullptr;

		std::unique_ptr<Buffer> pShaderBindingTable = std::make_unique<Buffer>(this, "Shader Binding Table");
		
		std::unique_ptr<OnlineDescriptorAllocator> pDescriptorAllocator = std::make_unique<OnlineDescriptorAllocator>(this, pContext, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		DescriptorHandle uavHandle = pDescriptorAllocator->AllocateTransientDescriptor(2);
		DescriptorHandle srvHandle = uavHandle + pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		std::unique_ptr<Buffer> pVertexBuffer = std::make_unique<Buffer>(this, "Vertex Buffer");
		Vector3 positions[] = {
			{ 0.0f, 0.25f, 0.0f},
			{ 0.25f, -0.25f, 0.0f},
			{ -0.25f, -0.25f, 0.0f},
		};
		pVertexBuffer->Create(BufferDesc::CreateVertexBuffer(3, sizeof(Vector3)));
		pVertexBuffer->SetData(pContext, positions, ARRAYSIZE(positions));

		// bottom level acceleration structure
		{
			D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc{};
			geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
			geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
			geometryDesc.Triangles.IndexBuffer = 0;
			geometryDesc.Triangles.IndexCount = 0;
			geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;
			geometryDesc.Triangles.Transform3x4 = 0;
			geometryDesc.Triangles.VertexBuffer.StartAddress = pVertexBuffer->GetGpuHandle();
			geometryDesc.Triangles.VertexBuffer.StrideInBytes = pVertexBuffer->GetDesc().ElementSize;
			geometryDesc.Triangles.VertexCount = pVertexBuffer->GetDesc().ElementCount;
			geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;

			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS prebuildInfo{};
			prebuildInfo.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
			prebuildInfo.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
			prebuildInfo.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
			prebuildInfo.NumDescs = 1;
			prebuildInfo.pGeometryDescs = &geometryDesc;

			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
			pDevice->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfo, &info);

			pBLASScratch = std::make_unique<Buffer>(this, "BLAS Scratch Buffer");
			pBLASScratch->Create(BufferDesc::CreateByteAddress(Math::AlignUp<int>(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::UnorderedAccess));
			pBLAS = std::make_unique<Buffer>(this, "BLAS");
			pBLAS->Create(BufferDesc::CreateAccelerationStructure(Math::AlignUp<int>(info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::UnorderedAccess));

			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc{};
			asDesc.Inputs = prebuildInfo;
			asDesc.DestAccelerationStructureData = pBLAS->GetGpuHandle();
			asDesc.ScratchAccelerationStructureData = pBLASScratch->GetGpuHandle();
			asDesc.SourceAccelerationStructureData = 0;

			pCmd->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
		}

		// top level acceleration structure
		{
			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS prebuildInfo{};
			prebuildInfo.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
			prebuildInfo.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
			prebuildInfo.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
			prebuildInfo.NumDescs = 1;

			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
			pDevice->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfo, &info);

			pTLASScratch = std::make_unique<Buffer>(this, "TLAS Scratch");
			pTLASScratch->Create(BufferDesc::CreateByteAddress(Math::AlignUp<int>(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::None));
			pTLAS = std::make_unique<Buffer>(this, "TLAS");
			pTLAS->Create(BufferDesc::CreateAccelerationStructure(Math::AlignUp<int>(info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)));
			
			pDescriptorsBuffer = std::make_unique<Buffer>(this, "Descriptors Buffer");
			pDescriptorsBuffer->Create(BufferDesc::CreateVertexBuffer(1, sizeof(D3D12_RAYTRACING_INSTANCE_DESC), BufferFlag::Upload));

			D3D12_RAYTRACING_INSTANCE_DESC* pInstanceDesc = static_cast<D3D12_RAYTRACING_INSTANCE_DESC*>(pDescriptorsBuffer->Map());
			pInstanceDesc->AccelerationStructure = pBLAS->GetGpuHandle();
			pInstanceDesc->Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
			pInstanceDesc->InstanceContributionToHitGroupIndex = 0;
			pInstanceDesc->InstanceID = 0;
			pInstanceDesc->InstanceMask = 0xFF;
			memcpy(pInstanceDesc->Transform, &Matrix::Identity, sizeof(Matrix));
			pDescriptorsBuffer->UnMap();

			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc{};
			asDesc.DestAccelerationStructureData = pTLAS->GetGpuHandle();
			asDesc.ScratchAccelerationStructureData = pTLASScratch->GetGpuHandle();
			asDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
			asDesc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
			asDesc.Inputs.InstanceDescs = pDescriptorsBuffer->GetGpuHandle();
			asDesc.Inputs.NumDescs = 1;
			asDesc.SourceAccelerationStructureData = 0;

			pCmd->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
		}

		// raytracing pipeline
		{
			pRayGenSignature->SetDescriptorTableSimple(0, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
			pRayGenSignature->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, D3D12_SHADER_VISIBILITY_ALL);
			pRayGenSignature->Finalize("Ray Gen RS", pDevice.Get(), D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);
			pHitSignature->Finalize("Ray Hit RS", pDevice.Get(), D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);
			pMissSignature->Finalize("Ray MissHit RS", pDevice.Get(), D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);
			pDummySignature->Finalize("Ray Dummy Global RS", pDevice.Get(), D3D12_ROOT_SIGNATURE_FLAG_NONE);
			
			ShaderLibrary rayGenShader("Resources/Shaders/RayTracingShaders/RayGen.hlsl");
			ShaderLibrary hitShader("Resources/Shaders/RayTracingShaders/Hit.hlsl");
			ShaderLibrary missShader("Resources/Shaders/RayTracingShaders/Miss.hlsl");

			CD3DX12_STATE_OBJECT_DESC desc(D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);
			{
				CD3DX12_DXIL_LIBRARY_SUBOBJECT* pRayGenDesc = desc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
				auto shaderBytecode_rayGen = CD3DX12_SHADER_BYTECODE(rayGenShader.GetByteCode(), rayGenShader.GetByteCodeSize());
				pRayGenDesc->SetDXILLibrary(&shaderBytecode_rayGen);
				pRayGenDesc->DefineExport(L"RayGen");

				CD3DX12_DXIL_LIBRARY_SUBOBJECT* pHitDesc = desc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
				auto shaderBytecode_hit = CD3DX12_SHADER_BYTECODE(hitShader.GetByteCode(), hitShader.GetByteCodeSize());
				pHitDesc->SetDXILLibrary(&shaderBytecode_hit);
				pHitDesc->DefineExport(L"ClosestHit");

				CD3DX12_DXIL_LIBRARY_SUBOBJECT* pMissDesc = desc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
				auto shaderBytecode_miss = CD3DX12_SHADER_BYTECODE(missShader.GetByteCode(), missShader.GetByteCodeSize());
				pMissDesc->SetDXILLibrary(&shaderBytecode_miss);
				pMissDesc->DefineExport(L"Miss");
			}

			{
				CD3DX12_HIT_GROUP_SUBOBJECT* pHitGroupDesc = desc.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
				pHitGroupDesc->SetHitGroupExport(L"HitGroup");
				pHitGroupDesc->SetClosestHitShaderImport(L"ClosestHit");
			}

			{
				CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT* pRayGenRs = desc.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
				pRayGenRs->SetRootSignature(pRayGenSignature->GetRootSignature());
				CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT* pRayGenAssociation = desc.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
				pRayGenAssociation->AddExport(L"RayGen");
				pRayGenAssociation->SetSubobjectToAssociate(*pRayGenRs);

				CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT* pMissRs = desc.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
				pMissRs->SetRootSignature(pMissSignature->GetRootSignature());
				CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT* pMissAssociation = desc.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
				pMissAssociation->AddExport(L"Miss");
				pMissAssociation->SetSubobjectToAssociate(*pMissRs);

				CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT* pHitRs = desc.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
				pMissRs->SetRootSignature(pHitSignature->GetRootSignature());
				CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT* pHitAssociation = desc.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
				pHitAssociation->AddExport(L"HitGroup");
				pHitAssociation->SetSubobjectToAssociate(*pHitRs);
			}

			{
				CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT* pRtConfig = desc.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
				pRtConfig->Config(4 * sizeof(float), 2 * sizeof(float));

				CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT* pRtPipelineConfig = desc.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
				pRtPipelineConfig->Config(1);

				CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT* pGlobalRs = desc.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
				pGlobalRs->SetRootSignature(pDummySignature->GetRootSignature());
			}
			D3D12_STATE_OBJECT_DESC stateObject = *desc;
			HR(pDevice->CreateStateObject(&stateObject, IID_PPV_ARGS(pPipeline.GetAddressOf())));
			HR(pPipeline->QueryInterface(IID_PPV_ARGS(pPipelineProperties.GetAddressOf())));
		}

		// output texture
		{
			pOutputTexture->Create(TextureDesc::Create2D(m_WindowWidth, m_WindowHeight, DXGI_FORMAT_R8G8B8A8_UNORM, TextureFlag::UnorderedAccess));
			pOutputTexture->CreateUAV(&pOutputRawUAV, TextureUAVDesc(0));
		}

		// copy descriptors
		{
			pDevice->CopyDescriptorsSimple(1, uavHandle.GetCpuHandle(), pOutputTexture->GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			pDevice->CopyDescriptorsSimple(1, srvHandle.GetCpuHandle(), pTLAS->GetSRV()->GetDescriptor(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}

		int rayGenSize = 0;
		// shader binding
		{
			int64_t progIdSize = D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT;
			int64_t totalSize = 0;

			struct SBTEntry
			{
				SBTEntry(const std::wstring& entryPoint, const std::vector<void*>& inputData)
					: EntryPoint(entryPoint), InputData(inputData)
				{}

				std::wstring EntryPoint;
				std::vector<void*> InputData;
				int Size{0};
			};

			std::vector<SBTEntry> entries;
			entries.emplace_back(SBTEntry(L"RayGen", { reinterpret_cast<uint64_t*>(uavHandle.GetGpuHandle().ptr), reinterpret_cast<uint64_t*>(srvHandle.GetGpuHandle().ptr) }));
			entries.emplace_back(SBTEntry(L"Miss", { }));
			entries.emplace_back(SBTEntry(L"HitGroup", { }));

			for (SBTEntry& entry : entries)
			{
				entry.Size = Math::AlignUp<int64_t>(D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT + 8 * entry.InputData.size(), D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
				totalSize += entry.Size;
			}
			rayGenSize = entries[0].Size;

			pShaderBindingTable->Create(BufferDesc::CreateVertexBuffer(Math::AlignUp<int64_t>(totalSize, 256), 1, BufferFlag::Upload));
			char* pData = (char*)pShaderBindingTable->Map();
			for (SBTEntry& entry : entries)
			{
				void* id = pPipelineProperties->GetShaderIdentifier(entry.EntryPoint.c_str());
				memcpy(pData, id, progIdSize);
				memcpy(pData + progIdSize, entry.InputData.data(), entry.InputData.size() * 8);
				pData += entry.Size;
			}
			pShaderBindingTable->UnMap();
		}

		// dispatch rays
		{
			D3D12_DISPATCH_RAYS_DESC rayDesc{};
			rayDesc.Width = pOutputTexture->GetWidth();
			rayDesc.Height = pOutputTexture->GetHeight();
			rayDesc.Depth = 1;
			rayDesc.RayGenerationShaderRecord.StartAddress = pShaderBindingTable->GetGpuHandle();
			rayDesc.RayGenerationShaderRecord.SizeInBytes = rayGenSize;

			pContext->InsertResourceBarrier(pOutputTexture.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			//pContext->ClearUavUInt(pOutputTexture.get(), pOutputRawUAV);
			pContext->FlushResourceBarriers();

			pCmd->SetPipelineState1(pPipeline.Get());
			pCmd->DispatchRays(&rayDesc);
		}

		pContext->Execute(true);
	}

	m_pNoiseTexture = std::make_unique<GraphicsTexture>(this, "Noise Texture");
	m_pNoiseTexture->Create(pCommandContext, "Resources/Textures/Noise.png", false);

	pCommandContext->Execute(true);
}

void Graphics::UpdateImGui()
{
	m_FrameTimes[m_Frame % m_FrameTimes.size()] = GameTimer::DeltaTime();

	ImGui::Begin("SSAO");
	Vector2 image((float)m_pSSAOTarget->GetWidth(), (float)m_pSSAOTarget->GetHeight());
	Vector2 windowSize(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);
	float width = windowSize.x;
	float height = windowSize.x * image.y / image.x;
	if (image.x / windowSize.x < image.y / windowSize.y)
	{
		width = image.x / image.y * windowSize.y;
		height = windowSize.y;
	}

	ImTextureID user_texture_id = m_pSSAOTarget->GetSRV().ptr;
	ImGui::Image(user_texture_id, ImVec2(width, height));
	ImGui::End();

	ImGui::SetNextWindowPos(ImVec2(0, 0), 0, ImVec2(0, 0));
	ImGui::SetNextWindowSize(ImVec2(300, (float)m_WindowHeight));
	ImGui::Begin("GPU Stats", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	ImGui::Text("MS: %.4f", GameTimer::DeltaTime() * 1000.0f);
	ImGui::SameLine(100);
	ImGui::Text("FPS: %.1f", 1.0f / GameTimer::DeltaTime());
	ImGui::PlotLines("Frametime", m_FrameTimes.data(), (int)m_FrameTimes.size(), m_Frame % m_FrameTimes.size(), 0, 0.0f, 0.03f, ImVec2(200, 100));

	if (ImGui::Button("PIX Capture"))
	{
		m_StartPixCapture = true;
	}

	if (ImGui::TreeNodeEx("Lighting", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Combo("Render Path", (int*)&m_RenderPath, [](void* data, int index, const char** outText)
			{
				RenderPath path = (RenderPath)index;
				switch (path)
				{
				case RenderPath::Tiled:
					*outText = "Tiles";
					break;
				case RenderPath::Clustered:
					*outText = "Clustered";
					break;
				default:
					break;
				}
			return true;
			}, nullptr, 2);
		
		extern bool gVisualizeClusters;
		ImGui::Checkbox("Visualize Clusters", &gVisualizeClusters);

		ImGui::Separator();
		ImGui::SliderInt("Lights", &m_DesiredLightCount, 10, 16384 * 10);
		if (ImGui::Button("Generate Lights"))
		{
			RandomizeLights(m_DesiredLightCount);
		}

		ImGui::SliderFloat("Min Log Luminance", &g_MinLogLuminance, -100, 20);
		ImGui::SliderFloat("Max Log Luminance", &g_MaxLogLuminance, -50, 50);
		ImGui::SliderFloat("White Point", &g_WhitePoint, 0, 20);
		ImGui::SliderFloat("Tau", &g_Tau, 0, 100);

		ImGui::SliderFloat("AO Power", &g_AoPower, 1, 10);
		ImGui::SliderFloat("AO Threshold", &g_AoThreshold, 0, 0.025f);
		ImGui::SliderFloat("AO Radius", &g_AoRadius, 0.1f, 5.0f);
		ImGui::SliderInt("AO Samples", &g_AoSamples, 0, 64);

		if (ImGui::Button("Dump RenderGraph"))
		{
			gDumpRenderGraph = true;
		}

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Descriptor Heaps", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Text("Used CPU Descriptor Heaps");

		for (const auto& pAllocator : m_DescriptorHeaps)
		{
			switch (pAllocator->GetType())
			{
			case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
				ImGui::TextWrapped("CBV_SRV_UAV");
				break;
			case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
				ImGui::TextWrapped("Sampler");
				break;
			case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
				ImGui::TextWrapped("RTV");
				break;
			case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
				ImGui::TextWrapped("DSV");
				break;
			default:
				break;
			}

			uint32_t totalDescriptors = pAllocator->GetNumDescriptors();
			uint32_t usedDescriptors = pAllocator->GetNumAllocatedDescriptors();
			std::stringstream str;
			str << usedDescriptors << "/" << totalDescriptors;
			ImGui::ProgressBar((float)usedDescriptors / totalDescriptors, ImVec2(-1, 0), str.str().c_str());
		}
		ImGui::TreePop();
	}

	ImGui::End();

	static bool showOutputLog = true;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
	ImGui::SetNextWindowPos(ImVec2(300, showOutputLog ? (float)m_WindowHeight - 250 : (float)m_WindowHeight - 20));
	ImGui::SetNextWindowSize(ImVec2(showOutputLog ? (float)(m_WindowWidth - 250) * 0.5f : (float)m_WindowWidth - 250, 250));
	ImGui::SetNextWindowCollapsed(!showOutputLog);

	showOutputLog = ImGui::Begin("Output Log", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	if (showOutputLog)
	{
		ImGui::SetScrollHereY(1.0f);
		for (const Console::LogEntry& entry : Console::GetHistory())
		{
			switch (entry.Type)
			{
			case LogType::VeryVerbose:
			case LogType::Verbose:
			case LogType::Info:
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
				ImGui::TextWrapped("[Info] %s", entry.Message.c_str());
				break;
			case LogType::Warning:
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 0, 1));
				ImGui::TextWrapped("[Warning] %s", entry.Message.c_str());
				break;
			case LogType::Error:
			case LogType::FatalError:
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0, 0, 1));
				ImGui::TextWrapped("[Error] %s", entry.Message.c_str());
				break;
			}
			ImGui::PopStyleColor();
		}
	}
	ImGui::End();

	if (showOutputLog)
	{
		ImGui::SetNextWindowPos(ImVec2(250.f + (m_WindowWidth - 250) * 0.5f, (float)(showOutputLog ? m_WindowHeight - 250 : m_WindowHeight - 20)));
		ImGui::SetNextWindowSize(ImVec2((m_WindowWidth - 250) * 0.5f, 250));
		ImGui::SetNextWindowCollapsed(!showOutputLog);
		ImGui::Begin("Profiler", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
		ProfileNode* pRootNode = Profiler::Instance()->GetRootNode();
		pRootNode->RenderImGui(m_Frame);
		ImGui::End();
	}

	ImGui::PopStyleVar();
}

void Graphics::RandomizeLights(int count)
{
	m_Lights.resize(count);

	BoundingBox sceneBounds;
	sceneBounds.Center = Vector3(0, 70, 0);
	sceneBounds.Extents = Vector3(140, 70, 60);

	int lightIndex = 0;
	
	Vector3 dir(-300, -300, -300);
	dir.Normalize();
	m_Lights[lightIndex] = Light::Directional(Vector3(300, 300, 300), dir, 0.1f);
	m_Lights[lightIndex].ShadowIndex = 0;

	int randomLightsStartIndex = lightIndex + 1;
	for (int i = randomLightsStartIndex; i < m_Lights.size(); i++)
	{
		Vector4 color(Math::RandomRange(0.f, 1.f), Math::RandomRange(0.f, 1.f), Math::RandomRange(0.f, 1.f), 1);

		Vector3 position;
		position.x = Math::RandomRange(-sceneBounds.Extents.x, sceneBounds.Extents.x) + sceneBounds.Center.x;
		position.y = Math::RandomRange(-sceneBounds.Extents.y, sceneBounds.Extents.y) + sceneBounds.Center.y;
		position.z = Math::RandomRange(-sceneBounds.Extents.z, sceneBounds.Extents.z) + sceneBounds.Center.z;

		const float range = Math::RandomRange(4.f, 6.f);
		const float angle = Math::RandomRange(40.f, 80.f);

		Light::Type type = (rand() % 2 == 0) ? Light::Type::Point : Light::Type::Spot;
		switch (type)
		{
		case Light::Type::Point:
			m_Lights[i] = Light::Point(position, range, 4.0f, 0.5f, color);
			break;
		case Light::Type::Spot:
			m_Lights[i] = Light::Spot(position, range, Math::RandVector(), angle, 4.0f, 0.5f, color);
			break;
		case Light::Type::Directional:
		case Light::Type::MAX:
		default:
			break;
		}
	}

	std::sort(m_Lights.begin() + randomLightsStartIndex, m_Lights.end(), [](const Light& a, const Light b) { return (int)a.LightType < (int)b.LightType; });
	
	IdleGPU();
	if (m_pLightBuffer->GetDesc().ElementCount != count)
	{
		m_pLightBuffer->Create(BufferDesc::CreateStructured(count, sizeof(Light)));
	}
	CommandContext* pContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_pLightBuffer->SetData(pContext, m_Lights.data(), sizeof(Light) * m_Lights.size());
	pContext->Execute(true);
}

CommandQueue* Graphics::GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const
{
	return m_CommandQueues.at(type).get();
}

CommandContext* Graphics::AllocateCommandContext(D3D12_COMMAND_LIST_TYPE type)
{
	int typeIndex = type;

	std::scoped_lock lockGuard(m_ContextAllocationMutex);
	if (m_FreeCommandContexts[typeIndex].size() > 0)
	{
		CommandContext* pCommandContext = m_FreeCommandContexts[typeIndex].front();
		m_FreeCommandContexts[typeIndex].pop();
		pCommandContext->Reset();
		return pCommandContext;
	}
	else
	{
		ComPtr<ID3D12CommandList> pCommandList;
		ID3D12CommandAllocator* pAllocator = m_CommandQueues[type]->RequestAllocator();
		m_pDevice->CreateCommandList(0, type, pAllocator, nullptr, IID_PPV_ARGS(&pCommandList));
		m_CommandLists.push_back(std::move(pCommandList));

		m_CommandListPool[typeIndex].emplace_back(std::make_unique<CommandContext>(this, static_cast<ID3D12GraphicsCommandList*>(m_CommandLists.back().Get()), pAllocator, type));

		return m_CommandListPool[typeIndex].back().get();
	}
}

void Graphics::WaitForFence(uint64_t fenceValue)
{
	D3D12_COMMAND_LIST_TYPE type = (D3D12_COMMAND_LIST_TYPE)(fenceValue >> 56);
	CommandQueue* pQueue = GetCommandQueue(type);
	pQueue->WaitForFence(fenceValue);
}

void Graphics::FreeCommandList(CommandContext * pCommandContext)
{
	std::scoped_lock lockGuard(m_ContextAllocationMutex);
	m_FreeCommandContexts[pCommandContext->GetType()].push(pCommandContext);
}

void Graphics::IdleGPU()
{
	for (auto& pCommandQueue : m_CommandQueues)
	{
		if (pCommandQueue)
		{
			pCommandQueue->WaitForIdle();
		}
	}
}

bool Graphics::BeginPixCapture() const
{
	ComPtr<IDXGraphicsAnalysis> pAnalysis;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(pAnalysis.GetAddressOf()))))
	{
		pAnalysis->BeginCapture();
		return true;
	}
	return false;
}

bool Graphics::EndPixCapture() const
{
	ComPtr<IDXGraphicsAnalysis> pAnalysis;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(pAnalysis.GetAddressOf()))))
	{
		pAnalysis->EndCapture();
		return true;
	}
	return false;
}

bool Graphics::CheckTypedUAVSupport(DXGI_FORMAT format) const
{
	D3D12_FEATURE_DATA_D3D12_OPTIONS featureData{};
	HR(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &featureData, sizeof(featureData)));

	switch (format)
	{
	case DXGI_FORMAT_R32_FLOAT:
	case DXGI_FORMAT_R32_UINT:
	case DXGI_FORMAT_R32_SINT:
		// Unconditionally supported.
		return true;

	case DXGI_FORMAT_R32G32B32A32_FLOAT:
	case DXGI_FORMAT_R32G32B32A32_UINT:
	case DXGI_FORMAT_R32G32B32A32_SINT:
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
	case DXGI_FORMAT_R16G16B16A16_UINT:
	case DXGI_FORMAT_R16G16B16A16_SINT:
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UINT:
	case DXGI_FORMAT_R8G8B8A8_SINT:
	case DXGI_FORMAT_R16_FLOAT:
	case DXGI_FORMAT_R16_UINT:
	case DXGI_FORMAT_R16_SINT:
	case DXGI_FORMAT_R8_UNORM:
	case DXGI_FORMAT_R8_UINT:
	case DXGI_FORMAT_R8_SINT:
		// All these are supported if this optional feature is set.
		return featureData.TypedUAVLoadAdditionalFormats;

	case DXGI_FORMAT_R16G16B16A16_UNORM:
	case DXGI_FORMAT_R16G16B16A16_SNORM:
	case DXGI_FORMAT_R32G32_FLOAT:
	case DXGI_FORMAT_R32G32_UINT:
	case DXGI_FORMAT_R32G32_SINT:
	case DXGI_FORMAT_R10G10B10A2_UNORM:
	case DXGI_FORMAT_R10G10B10A2_UINT:
	case DXGI_FORMAT_R11G11B10_FLOAT:
	case DXGI_FORMAT_R8G8B8A8_SNORM:
	case DXGI_FORMAT_R16G16_FLOAT:
	case DXGI_FORMAT_R16G16_UNORM:
	case DXGI_FORMAT_R16G16_UINT:
	case DXGI_FORMAT_R16G16_SNORM:
	case DXGI_FORMAT_R16G16_SINT:
	case DXGI_FORMAT_R8G8_UNORM:
	case DXGI_FORMAT_R8G8_UINT:
	case DXGI_FORMAT_R8G8_SNORM:
	case DXGI_FORMAT_R8G8_SINT:
	case DXGI_FORMAT_R16_UNORM:
	case DXGI_FORMAT_R16_SNORM:
	case DXGI_FORMAT_R8_SNORM:
	case DXGI_FORMAT_A8_UNORM:
	case DXGI_FORMAT_B5G6R5_UNORM:
	case DXGI_FORMAT_B5G5R5A1_UNORM:
	case DXGI_FORMAT_B4G4R4A4_UNORM:
		// Conditionally supported by specific pDevices.
		if (featureData.TypedUAVLoadAdditionalFormats)
		{
			D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport = { format, D3D12_FORMAT_SUPPORT1_NONE, D3D12_FORMAT_SUPPORT2_NONE };
			HR(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &formatSupport, sizeof(formatSupport)));
			const DWORD mask = D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD | D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
			return ((formatSupport.Support2 & mask) == mask);
		}
		return false;

	default:
		return false;
	}
}

bool Graphics::UseRenderPasses() const
{
	return m_RenderPassTier > D3D12_RENDER_PASS_TIER::D3D12_RENDER_PASS_TIER_0;
}

bool Graphics::IsFenceComplete(uint64_t fenceValue)
{
	D3D12_COMMAND_LIST_TYPE type = (D3D12_COMMAND_LIST_TYPE)(fenceValue >> 56);
	CommandQueue* pQueue = GetCommandQueue(type);
	return pQueue->IsFenceComplete(fenceValue);
}

uint32_t Graphics::GetMultiSampleQualityLevel(uint32_t msaa)
{
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qualityLevels;
	qualityLevels.Format = RENDER_TARGET_FORMAT;
	qualityLevels.SampleCount = msaa;
	qualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	HR(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &qualityLevels, sizeof(qualityLevels)));
	return qualityLevels.NumQualityLevels - 1;
}

ID3D12Resource* Graphics::CreateResource(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType, D3D12_CLEAR_VALUE* pClearValue)
{
	ID3D12Resource* pResource;
	
	D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(heapType);
	HR(m_pDevice->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		initialState,
		pClearValue,
		IID_PPV_ARGS(&pResource)));
	
	return pResource;
}

