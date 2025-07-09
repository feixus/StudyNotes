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
#include "Scene/Camera.h"
#include "Clouds.h"
#include <DXProgrammableCapture.h>
#include "RenderGraph/RenderGraph.h"
#include "RenderGraph/Blackboard.h"
#include "RenderGraph/ResourceAllocator.h"
#include "DebugRenderer.h"

#ifdef _DEBUG
#define D3D_VALIDATION 1
#endif

#ifndef D3D_VALIDATION
#define D3D_VALIDATION 0
#endif

#ifndef GPU_VALIDATION
#define GPU_VALIDATION 0
#endif

bool gSortOpaqueMeshes = true;
bool gSortTransparentMeshes = true;
bool gDumpRenderGraph = true;

float g_WhitePoint = 4;
float g_MinLogLuminance = -10.0f;
float g_MaxLogLuminance = 2.0f;
float g_Tau = 10;

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

	// per frame constants
	////////////////////////////////////
	struct PerFrameData
	{
		Matrix ViewInverse;
		uint32_t LightCount{0};
	} frameData;

	// camera constants
	frameData.ViewInverse = m_pCamera->GetViewInverse();

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
	} sceneData;

	sceneData.DepthStencil = graph.ImportTexture("Depth Stencil", GetDepthStencil());
	sceneData.DepthStencilResolved = graph.ImportTexture("Depth Stencil Target", GetResolveDepthStencil());

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

					renderContext.BeginRenderPass(RenderPassInfo(pDepthStencil, RenderPassAccess::Clear_Store));
					renderContext.SetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
					renderContext.SetViewport(FloatRect(0, 0, (float)m_WindowWidth, (float)m_WindowHeight));

					renderContext.SetGraphicsPipelineState(m_pDepthPrepassPSO.get());
					renderContext.SetGraphicsRootSignature(m_pDepthPrepassRS.get());
					for (const Batch& b : m_OpaqueBatches)
					{
						Matrix worldViewProjection = b.WorldMatrix * m_pCamera->GetViewProjection();
						renderContext.SetDynamicConstantBufferView(0, &worldViewProjection, sizeof(Matrix));
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
				};
			});
	}

	if (m_RenderPath == RenderPath::Tiled)
	{
		// 3. light culling
		//  - compute shader to buckets lights in tiles depending on their screen position
		//  - require a depth buffer
		//  - outputs a: - Texture2D containing a count and an offset of lights per tile.
		//								- uint[] index buffer to indicate what are visible in each tile
		graph.AddPass("Light Culling", [&](RGPassBuilder& builder)
			{
				builder.NeverCull();
				sceneData.DepthStencilResolved = builder.Read(sceneData.DepthStencilResolved);

				return [=](CommandContext& context, const RGPassResource& resources)
					{
						{
							GPU_PROFILE_SCOPE("SetupLightData", &context);
							uint32_t zero[] = { 0, 0 };
							m_pLightIndexCounter->SetData(&context, &zero, sizeof(uint32_t) * 2);
							m_pLightBuffer->SetData(&context, m_Lights.data(), sizeof(Light) * (uint32_t)m_Lights.size());
						}

						context.InsertResourceBarrier(GetResolveDepthStencil(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

						context.SetComputePipelineState(m_pComputeLightCullPipeline.get());
						context.SetComputeRootSignature(m_pComputeLightCullRS.get());

						struct ShaderParameter
						{
							Matrix CameraView;
							Matrix ProjectionInverse;
							uint32_t NumThreadGroups[4]{};
							Vector2 ScreenDimensions;
							uint32_t LightCount{0};
						} Data;

						Data.CameraView = m_pCamera->GetView();
						Data.NumThreadGroups[0] = Math::DivideAndRoundUp(m_WindowWidth, FORWARD_PLUS_BLOCK_SIZE);
						Data.NumThreadGroups[1] = Math::DivideAndRoundUp(m_WindowHeight, FORWARD_PLUS_BLOCK_SIZE);
						Data.NumThreadGroups[2] = 1;
						Data.ScreenDimensions.x = (float)m_WindowWidth;
						Data.ScreenDimensions.y = (float)m_WindowHeight;
						Data.LightCount = (uint32_t)m_Lights.size();
						Data.ProjectionInverse = m_pCamera->GetProjectionInverse();

						context.SetComputeDynamicConstantBufferView(0, &Data, sizeof(ShaderParameter));
						context.SetDynamicDescriptor(1, 0, m_pLightIndexCounter->GetUAV());
						context.SetDynamicDescriptor(1, 1, m_pLightIndexListBufferOpaque->GetUAV());
						context.SetDynamicDescriptor(1, 2, m_pLightGridOpaque->GetUAV());
						context.SetDynamicDescriptor(1, 3, m_pLightIndexListBufferTransparent->GetUAV());
						context.SetDynamicDescriptor(1, 4, m_pLightGridTransparent->GetUAV());
						context.SetDynamicDescriptor(2, 0, GetResolveDepthStencil()->GetSRV());
						context.SetDynamicDescriptor(2, 1, m_pLightBuffer->GetSRV());

						context.Dispatch(Data.NumThreadGroups[0], Data.NumThreadGroups[1], Data.NumThreadGroups[2]);
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
								} ObjectData;

								// opaque
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

								// transparent
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

		// 5. base pass
		//  - render the scene using the shadow mapping result and the light culling buffers
		graph.AddPass("Base Pass", [&](RGPassBuilder& builder)
			{
				builder.NeverCull();
				return[=](CommandContext& context, const RGPassResource& resources)
					{
						context.SetViewport(FloatRect(0, 0, (float)m_WindowWidth, (float)m_WindowHeight));

						context.InsertResourceBarrier(m_pShadowMap.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
						context.InsertResourceBarrier(m_pLightGridOpaque.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
						context.InsertResourceBarrier(m_pLightIndexListBufferOpaque.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
						context.InsertResourceBarrier(m_pLightGridTransparent.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
						context.InsertResourceBarrier(m_pLightIndexListBufferTransparent.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
						context.InsertResourceBarrier(GetDepthStencil(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
						context.InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET);

						context.BeginRenderPass(RenderPassInfo(GetCurrentRenderTarget(), RenderPassAccess::Clear_Store, GetDepthStencil(), RenderPassAccess::Load_DontCare));

						context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
						context.SetGraphicsRootSignature(m_pPBRDiffuseRS.get());

						context.SetDynamicConstantBufferView(1, &frameData, sizeof(PerFrameData));
						context.SetDynamicConstantBufferView(2, &lightData, sizeof(LightData));
						context.SetDynamicDescriptor(4, 0, m_pShadowMap->GetSRV());
						context.SetDynamicDescriptor(4, 1, m_pLightGridOpaque->GetSRV());
						context.SetDynamicDescriptor(4, 2, m_pLightIndexListBufferOpaque->GetSRV());
						context.SetDynamicDescriptor(4, 3, m_pLightBuffer->GetSRV());

						struct PerObjectData
						{
							Matrix World;
							Matrix WorldViewProjection;
						} objectData;

						// opaque
						{
							GPU_PROFILE_SCOPE("Opaque", &context);
							context.SetGraphicsPipelineState(m_pPBRDiffusePSO.get());
							
							for (const Batch& b : m_OpaqueBatches)
							{
								objectData.World = b.WorldMatrix;
								objectData.WorldViewProjection = b.WorldMatrix * m_pCamera->GetViewProjection();
								context.SetDynamicConstantBufferView(0, &objectData, sizeof(PerObjectData));

								context.SetDynamicDescriptor(3, 0, b.pMaterial->pDiffuseTexture->GetSRV());
								context.SetDynamicDescriptor(3, 1, b.pMaterial->pNormalTexture->GetSRV());
								context.SetDynamicDescriptor(3, 2, b.pMaterial->pSpecularTexture->GetSRV());

								b.pMesh->Draw(&context);
							}
						}

						// transparent
						{
							GPU_PROFILE_SCOPE("Transparent", &context);
							context.SetGraphicsPipelineState(m_pPBRDiffuseAlphaPSO.get());
							
							for (const Batch& b : m_TransparentBatches)
							{
								objectData.World = b.WorldMatrix;
								objectData.WorldViewProjection = b.WorldMatrix * m_pCamera->GetViewProjection();
								context.SetDynamicConstantBufferView(0, &objectData, sizeof(PerObjectData));

								context.SetDynamicDescriptor(3, 0, b.pMaterial->pDiffuseTexture->GetSRV());
								context.SetDynamicDescriptor(3, 1, b.pMaterial->pNormalTexture->GetSRV());
								context.SetDynamicDescriptor(3, 2, b.pMaterial->pSpecularTexture->GetSRV());

								b.pMesh->Draw(&context);
							}
						}

						context.InsertResourceBarrier(m_pLightGridOpaque.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
						context.InsertResourceBarrier(m_pLightIndexListBufferOpaque.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
						context.InsertResourceBarrier(m_pLightGridTransparent.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
						context.InsertResourceBarrier(m_pLightIndexListBufferTransparent.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

						context.EndRenderPass();
					};
			});
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
		// exposure adjustment
		// luminance histogram, collect the pixel count into a 256 bins histogram with luminance
		graph.AddPass("Luminance Histogram", [&](RGPassBuilder& builder)
			{
				builder.NeverCull();

				return[=](CommandContext& context, const RGPassResource& resources)
					{
						context.InsertResourceBarrier(m_pLuminanceHistogram.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
						context.InsertResourceBarrier(m_pHDRRenderTarget.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
						context.FlushResourceBarriers();

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

						Parameters.Width = m_WindowWidth;
						Parameters.Height = m_WindowHeight;
						Parameters.MinLogLuminance = g_MinLogLuminance;
						Parameters.OneOverLogLuminanceRange = 1.0f / (g_MaxLogLuminance - g_MinLogLuminance);

						context.SetComputeDynamicConstantBufferView(0, &Parameters, sizeof(HistogramParameters));
						context.SetDynamicDescriptor(1, 0, m_pLuminanceHistogram->GetUAV());
						context.SetDynamicDescriptor(2, 0, m_pHDRRenderTarget->GetSRV());

						context.Dispatch(Math::RoundUp(m_WindowWidth / 16.0f), Math::RoundUp(m_WindowHeight / 16.0f), 1);
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

						AverageParameters.PixelCount = m_WindowWidth * m_WindowHeight;
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

						context.SetGraphicsPipelineState(m_pToneMapPSO.get());
						context.SetGraphicsRootSignature(m_pToneMapRS.get());
						context.SetViewport(FloatRect(0, 0, (float)m_WindowWidth, (float)m_WindowHeight));
						context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
						context.BeginRenderPass(RenderPassInfo(GetCurrentBackbuffer(), RenderPassAccess::Clear_Store, nullptr, RenderPassAccess::DontCare_DontCare));

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
	ComPtr<IDXGIAdapter1> pAdapter;
	while (m_pFactory->EnumAdapterByGpuPreference(adapter, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&pAdapter)) != DXGI_ERROR_NOT_FOUND)
	{
		DXGI_ADAPTER_DESC1 desc;
		pAdapter->GetDesc1(&desc);

		if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
		{
			char name[256];
			ToMultibyte(desc.Description, name, 256);
			E_LOG(LogType::Info, "Using %s", name);
			break;
		}

		pAdapter->Release();
		pAdapter = nullptr;
		++adapter;
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
 
 #if 0
		HR(pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SERVERITY_ERROR, true));
 #endif

 		pInfoQueue->PushStorageFilter(&NewFilter);
 		pInfoQueue->Release();
	}
#endif

	D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
	if (m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS5)) == S_OK)
	{
		m_RenderPassTier = options.RenderPassesTier;
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

	m_pLightGridOpaque = std::make_unique<GraphicsTexture>(this, "Opaque Light Grid");
	m_pLightGridTransparent = std::make_unique<GraphicsTexture>(this, "Transparent Light Grid");

	m_pClusteredForward = std::make_unique<ClusteredForward>(this);
	
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
		&pSwapChain));

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

	m_pHDRRenderTarget->Create(TextureDesc::Create2D(width, height, RENDER_TARGET_FORMAT, TextureFlag::ShaderResource));

	int frustumCountX = Math::RoundUp((float)m_WindowWidth / FORWARD_PLUS_BLOCK_SIZE);
	int frustumCountY = Math::RoundUp((float)m_WindowHeight / FORWARD_PLUS_BLOCK_SIZE);
	m_pLightGridOpaque->Create(TextureDesc::Create2D(frustumCountX, frustumCountY, DXGI_FORMAT_R32G32_UINT, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource));
	m_pLightGridTransparent->Create(TextureDesc::Create2D(frustumCountX, frustumCountY, DXGI_FORMAT_R32G32_UINT, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource));

	m_pCamera->SetDirty();

	m_pClusteredForward->OnSwapchainCreated(width, height);
	m_pClouds->OnSwapchainCreated(width, height);
}

void Graphics::InitializeAssets()
{
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

	// PBR diffuse passes
	{
		// shaders
		Shader vertexShader("Resources/Shaders/Diffuse.hlsl", Shader::Type::VertexShader, "VSMain", { "SHADOW" });
		Shader pixelShader("Resources/Shaders/Diffuse.hlsl", Shader::Type::PixelShader, "PSMain", { "SHADOW" });

		// root signature
		m_pPBRDiffuseRS = std::make_unique<RootSignature>();
		m_pPBRDiffuseRS->FinalizeFromShader("Diffuse PBR RS", vertexShader, m_pDevice.Get());

		// opaque
		{
			m_pPBRDiffusePSO = std::make_unique<GraphicsPipelineState>();
			m_pPBRDiffusePSO->SetInputLayout(inputElements, sizeof(inputElements) / sizeof(inputElements[0]));
			m_pPBRDiffusePSO->SetRootSignature(m_pPBRDiffuseRS->GetRootSignature());
			m_pPBRDiffusePSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
			m_pPBRDiffusePSO->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
			m_pPBRDiffusePSO->SetRenderTargetFormat(RENDER_TARGET_FORMAT, DEPTH_STENCIL_FORMAT, m_SampleCount, m_SampleQuality);
			m_pPBRDiffusePSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
			m_pPBRDiffusePSO->SetDepthWrite(false);
			m_pPBRDiffusePSO->Finalize("Diffuse PBR Pipeline", m_pDevice.Get());
		}

		// transparent
		{
			m_pPBRDiffuseAlphaPSO = std::make_unique<GraphicsPipelineState>(*m_pPBRDiffusePSO.get());
			m_pPBRDiffuseAlphaPSO->SetInputLayout(inputElements, sizeof(inputElements) / sizeof(inputElements[0]));
			m_pPBRDiffuseAlphaPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
			m_pPBRDiffuseAlphaPSO->Finalize("Diffuse PBR (Alpha) Pipeline", m_pDevice.Get());
		}
	}

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
		Shader vertexShader("Resources/Shaders/DepthOnly.hlsl", Shader::Type::VertexShader, "VSMain");

		// root signature
		m_pDepthPrepassRS = std::make_unique<RootSignature>();
		m_pDepthPrepassRS->FinalizeFromShader("Depth Prepass RS", vertexShader, m_pDevice.Get());

		// pipeline state
		m_pDepthPrepassPSO = std::make_unique<GraphicsPipelineState>();
		m_pDepthPrepassPSO->SetInputLayout(depthOnlyInputElements, sizeof(depthOnlyInputElements) / sizeof(depthOnlyInputElements[0]));
		m_pDepthPrepassPSO->SetRootSignature(m_pDepthPrepassRS->GetRootSignature());
		m_pDepthPrepassPSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pDepthPrepassPSO->SetRenderTargetFormats(nullptr, 0, DEPTH_STENCIL_FORMAT, m_SampleCount, m_SampleQuality);
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

	// light culling
	// compute shader that required depth buffer and light data to place lights into tiles
	{
		Shader computeShader("Resources/Shaders/LightCulling.hlsl", Shader::Type::ComputeShader, "CSMain");

		m_pComputeLightCullRS = std::make_unique<RootSignature>();
		m_pComputeLightCullRS->FinalizeFromShader("Light Culling", computeShader, m_pDevice.Get());

		m_pComputeLightCullPipeline = std::make_unique<ComputePipelineState>();
		m_pComputeLightCullPipeline->SetRootSignature(m_pComputeLightCullRS->GetRootSignature());
		m_pComputeLightCullPipeline->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pComputeLightCullPipeline->Finalize("Compute Light Culling Pipeline", m_pDevice.Get());

		m_pLightIndexCounter = std::make_unique<Buffer>(this, "Light Index Counter");
		m_pLightIndexCounter->Create(BufferDesc::CreateStructured(2, sizeof(uint32_t)));
		m_pLightIndexListBufferOpaque = std::make_unique<Buffer>(this, "Light List Opaque");
		m_pLightIndexListBufferOpaque->Create(BufferDesc::CreateStructured(MAX_LIGHT_DENSITY, sizeof(uint32_t)));
		m_pLightIndexListBufferTransparent = std::make_unique<Buffer>(this, "Light List Transparent");
		m_pLightIndexListBufferTransparent->Create(BufferDesc::CreateStructured(MAX_LIGHT_DENSITY, sizeof(uint32_t)));
		m_pLightBuffer = std::make_unique<Buffer>(this, "Light Buffer");
	}

	// geometry
	{
		CommandContext* pCommandContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COPY);
		
		m_pMesh = std::make_unique<Mesh>();
		m_pMesh->Load("Resources/sponza/sponza.dae", this, pCommandContext);

		pCommandContext->Execute(true);

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
}

void Graphics::UpdateImGui()
{
	m_FrameTimes[m_Frame % m_FrameTimes.size()] = GameTimer::DeltaTime();
	
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
		ImGui::SliderInt("Lights", &m_DesiredLightCount, 10, 16384 * 4);
		if (ImGui::Button("Generate Lights"))
		{
			RandomizeLights(m_DesiredLightCount);
		}

		ImGui::SliderFloat("Min Log Luminance", &g_MinLogLuminance, -100, 20);
		ImGui::SliderFloat("Max Log Luminance", &g_MaxLogLuminance, -50, 50);
		ImGui::SliderFloat("White Point", &g_WhitePoint, 0, 20);
		ImGui::SliderFloat("Tau", &g_Tau, 0, 100);

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

		const float range = Math::RandomRange(8.f, 12.f);
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

