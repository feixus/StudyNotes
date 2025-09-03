#include "stdafx.h"
#include "Graphics.h"
#include "CommandQueue.h"
#include "CommandContext.h"
#include "OfflineDescriptorAllocator.h"
#include "DynamicResourceAllocator.h"
#include "GraphicsTexture.h"
#include "GraphicsBuffer.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "Shader.h"
#include "ResourceViews.h"
#include "Core/Input.h"
#include "Scene/Camera.h"
#include "Graphics/ImGuiRenderer.h"
#include "Graphics/Mesh.h"
#include "Graphics/Profiler.h"
#include "Graphics/DebugRenderer.h"
#include "Graphics/Techniques/ClusteredForward.h"
#include "Graphics/Techniques/TiledForward.h"
#include "Graphics/Techniques/Clouds.h"
#include "Graphics/Techniques/GpuParticles.h"
#include "Graphics/Techniques/RTAO.h"
#include "Graphics/Techniques/SSAO.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/Blackboard.h"
#include "Graphics/RenderGraph/ResourceAllocator.h"
#include "Core/CommandLine.h"
#include "Core/TaskQueue.h"
#include "Content/image.h"

#define STB_RECT_PACK_IMPLEMENTATION
#include "External/imgui/imstb_rectpack.h"

#ifdef _DEBUG
#define D3D_VALIDATION 1
#endif

#ifndef D3D_VALIDATION
#define D3D_VALIDATION 0
#endif

#ifndef GPU_VALIDATION
#define GPU_VALIDATION 0
#endif

bool g_DumpRenderGraph = false;
bool g_Screenshot = false;

float g_WhitePoint = 1;
float g_MinLogLuminance = -10.0f;
float g_MaxLogLuminance = 20.0f;
float g_Tau = 2;
uint32_t g_ToneMapper = 2;
bool g_DrawHistogram = false;

bool g_ShowSDSM = false;
bool g_StabilizeCascases = true;
float g_PSSMFactor = 1.0f;

bool g_ShowRaytraced = false;
bool g_VisualizeLights = false;

float g_SunInclination = 0.2f;
float g_SunOrientation = -3.055f;
float g_SunTemperature = 5000.0f;

bool g_EnableUI = true;

Graphics::Graphics(uint32_t width, uint32_t height, int sampleCount) :
	m_WindowWidth(width), m_WindowHeight(height), m_SampleCount(sampleCount)
{}

Graphics::~Graphics()
{}

void Graphics::Initialize(HWND hWnd)
{
	m_pWindow = hWnd;

	m_pCamera = std::make_unique<FreeCamera>();
	m_pCamera->SetPosition(Vector3(0, 100, -15));
	m_pCamera->SetRotation(Quaternion::CreateFromYawPitchRoll(Math::PIDIV4, Math::PIDIV4, 0));
	m_pCamera->SetNewPlane(500.f);
	m_pCamera->SetFarPlane(10.f);

	InitD3D();
	InitializePipelines();

	CommandContext* pContext = AllocateCommandContext();
	InitializeAssets(*pContext);
	pContext->Execute(true);

	g_ShowRaytraced = SupportsRaytracing();

	m_DesiredLightCount = 3;
	RandomizeLights(m_DesiredLightCount);

	m_pDynamicAllocationManager->FlushAll();
}

void Graphics::Update()
{
	PROFILE_BEGIN("Update");
	BeginFrame();
	m_pImGuiRenderer->Update();

	PROFILE_BEGIN("UpdateGameState");

	m_pCamera->Update();

	float costheta = cosf(g_SunOrientation);
	float sintheta = sinf(g_SunOrientation);
	float cosphi = cosf(g_SunInclination * Math::PIDIV2);
	float sinphi = sinf(g_SunInclination * Math::PIDIV2);
	m_Lights[0].Direction = -Vector3(costheta * sinphi, cosphi, sintheta * sinphi);
	m_Lights[0].Colour = Math::EncodeColor(Math::MakeFromColorTemperature(g_SunTemperature));

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

	if (g_VisualizeLights)
	{
		for (const auto& light : m_Lights)
		{
			DebugRenderer::Get()->AddLight(light);
		}
	}

	if (Input::Instance().IsKeyPressed('U'))
	{
		g_EnableUI = !g_EnableUI;
	}

	m_Lights[1].Position.x = 50 * sin(Time::GameTime());

	// shadow map partitioning
	//////////////////////////////////

	ShadowData shadowData;
	float minPoint = 0;
	float maxPoint = 1;
	constexpr uint32_t MAX_CASCADES = 4;
	std::array<float, MAX_CASCADES> cascadeSplits;

	if (g_ShowSDSM)
	{
		Buffer* pSourceBuffer = m_ReductionReadbackTargets[(m_Frame + 1) % FRAME_COUNT].get();
		float* pData = (float*)pSourceBuffer->Map();
		minPoint = pData[0];
		maxPoint = pData[1];
		pSourceBuffer->UnMap();
	}

	float nearPlane = m_pCamera->GetFar();
	float farPlane = m_pCamera->GetNear();
	float clipPlaneRange = farPlane - nearPlane;

	float minZ = nearPlane + minPoint * clipPlaneRange;
	float maxZ = nearPlane + maxPoint * clipPlaneRange;

	for (uint32_t i = 0; i < MAX_CASCADES; i++)
	{
		float p = (i + 1) / (float)MAX_CASCADES;
		float log = minZ * std::pow(maxZ / minZ, p);
		float uniform = minZ + (maxZ - minZ) * p;
		float d = g_PSSMFactor * (log - uniform) + uniform;
		cascadeSplits[i] = (d - nearPlane) / clipPlaneRange;
	}

	stbrp_context context;
	stbrp_node nodes[64];
	stbrp_init_target(&context, m_pShadowMap->GetWidth(), m_pShadowMap->GetHeight(), nodes, 64);
	std::vector<stbrp_rect> rects;

	int shadowIndex = 0;
	for (size_t i = 0; i < m_Lights.size(); ++i)
	{
		Light& light = m_Lights[i];
		if (light.ShadowIndex == -1)
		{
			continue;
		}
		light.ShadowIndex = shadowIndex;
		if (light.LightType == Light::Type::Directional)
		{
			for (uint32_t i = 0; i < MAX_CASCADES; ++i)
			{
				float previousCascadeSplit = i == 0 ? minPoint : cascadeSplits[i - 1];
				float currentCascadeSplit = cascadeSplits[i];

				Vector3 frustumCorners[] = {
					// near
					Vector3(-1, -1, 1),
					Vector3(-1,  1, 1),
					Vector3( 1,  1, 1),
					Vector3( 1, -1, 1),
					// far
					Vector3(-1, -1, 0),
					Vector3(-1,  1, 0),
					Vector3( 1,  1, 0),
					Vector3( 1, -1, 0),
				};

				// retrieve frustum corners in world space
				for (Vector3& corner : frustumCorners)
				{
					corner = Vector3::Transform(corner, m_pCamera->GetProjectionInverse());
					corner = Vector3::Transform(corner, m_pCamera->GetViewInverse());
				}

				// adjust frustum corners based on cascade splits
				for (int j = 0; j < 4; j++)
				{
					Vector3 cornerRay = frustumCorners[j + 4] - frustumCorners[j];
					Vector3 nearPoint = previousCascadeSplit * cornerRay;
					Vector3 farPoint = currentCascadeSplit * cornerRay;
					frustumCorners[j + 4] = frustumCorners[j] + farPoint;
					frustumCorners[j] = frustumCorners[j] + nearPoint;
				}

				Vector3 center = Vector3::Zero;
				for (const Vector3& corner : frustumCorners)
				{
					center += corner;
				}

				center /= std::size(frustumCorners);

				Vector3 minExtents(FLT_MAX);
				Vector3 maxExtents(-FLT_MAX);

				// create a bounding sphere to maintain aspect in projection to avoid flickering when rotating
				if (g_StabilizeCascases)
				{
					float radius = 0;
					for (const Vector3& corner : frustumCorners)
					{
						float distS = Vector3::DistanceSquared(corner, center);
						radius = Math::Max(radius, distS);
					}
					radius = std::sqrt(radius);
					maxExtents  = Vector3(radius);
					minExtents  = -maxExtents;
				}
				else
				{
					Matrix lightView = XMMatrixLookToLH(center, m_Lights[0].Direction, Vector3::Up);
					for (const Vector3& corner : frustumCorners)
					{
						Vector3 transformedCorner = Vector3::Transform(corner, lightView);
						minExtents = Vector3::Min(minExtents, transformedCorner);
						maxExtents = Vector3::Max(maxExtents, transformedCorner);
					}
				}

				Matrix shadowView = XMMatrixLookToLH(center + light.Direction * -400, light.Direction, Vector3::Up);
				Matrix projectionMatrix = Math::CreateOrthographicOffCenterMatrix(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, maxExtents.z + 400, 0);
				Matrix lightViewProjection = shadowView * projectionMatrix;

				// snap projection to shadowmap texels to avoid flickering edges
				if (g_StabilizeCascases)
				{
					float shadowMapSize = m_pShadowMap->GetHeight() * 0.5f;
					Vector4 shadowOrigin = Vector4::Transform(Vector4(0, 0, 0, 1), lightViewProjection);
					shadowOrigin *= shadowMapSize * 0.5f;
					Vector4 rounded = XMVectorRound(shadowOrigin);
					Vector4 roundedOffset = rounded - shadowOrigin;
					roundedOffset *= 2.0f / shadowMapSize;
					roundedOffset.z = 0;
					roundedOffset.w = 0;

					projectionMatrix *= Matrix::CreateTranslation(Vector3(roundedOffset));
					lightViewProjection = shadowView * projectionMatrix;
				}

				stbrp_rect rect;
				rect.w = 1024;
				rect.h = 1024;
				rect.id = shadowIndex;
				rects.push_back(rect);
			
				shadowData.CascadeDepths[shadowIndex] = currentCascadeSplit * (farPlane - nearPlane) + nearPlane;
				shadowData.LightViewProjections[shadowIndex++] = lightViewProjection;
			}
		}
		else if (light.LightType == Light::Type::Spot)
		{
			stbrp_rect rect;
			rect.w = 1024;
			rect.h = 1024;
			rect.id = shadowIndex;
			rects.push_back(rect);

			Matrix projection = DirectX::XMMatrixPerspectiveFovLH(2 * acos(light.SpotlightAngles.y), 1.0f, light.Range, 5.0f);
			shadowData.LightViewProjections[shadowIndex++] = (Matrix)(XMMatrixLookToLH(light.Position, light.Direction, Vector3::Up)) * projection;
		}
		else if (light.LightType == Light::Type::Point)
		{
			Matrix projection = Math::CreatePerspectiveMatrix(Math::PIDIV2, 1, light.Range, 5.0f);

			constexpr Vector3 cubemapDirections[] = {
				Vector3(-1, 0, 0),
				Vector3(1, 0, 0),
				Vector3(0, -1, 0),
				Vector3(0, 1, 0),
				Vector3(0, 0, -1),
				Vector3(0, 0, 1),
			};
			constexpr Vector3 cubemapUpDirections[] = {
				Vector3(0, 1, 0),
				Vector3(0, 1, 0),
				Vector3(0, 0, -1),
				Vector3(0, 0, 1),
				Vector3(0, 1, 0),
				Vector3(0, 1, 0),
			};

			for (int j = 0; j < 6; ++j)
			{
				stbrp_rect rect;
				rect.w = 1024;
				rect.h = 1024;
				rect.id = j + shadowIndex;
				rects.push_back(rect);

				shadowData.LightViewProjections[shadowIndex + j] = Matrix::CreateLookAt(light.Position, light.Position + cubemapDirections[j], cubemapDirections[j]) * projection;
			}
			shadowIndex += 6;
		}
	}

	stbrp_pack_rects(&context, rects.data(), rects.size());

	for (const stbrp_rect& r : rects)
	{
		shadowData.ShadowMapOffsets[r.id].x = (float)r.x / m_pShadowMap->GetWidth();
		shadowData.ShadowMapOffsets[r.id].y = (float)r.y / m_pShadowMap->GetHeight();
		shadowData.ShadowMapOffsets[r.id].z = (float)r.w / m_pShadowMap->GetWidth();
		shadowData.ShadowMapOffsets[r.id].w = (float)r.h / m_pShadowMap->GetHeight();
	}
	
	PROFILE_END();

	////////////////////////////////
	// Rendering Begin
	////////////////////////////////

	if (m_CapturePix)
	{
		D3D::BeginPixCapture();
	}

	RGGraph graph(this);

	struct MainData
	{
		RGResourceHandle DepthStencil;
		RGResourceHandle DepthStencilResolved;
	} sceneData{};

	sceneData.DepthStencil = graph.ImportTexture("Depth Stencil", GetDepthStencil());
	sceneData.DepthStencilResolved = graph.ImportTexture("Resolved Depth Stencil", GetResolveDepthStencil());

	uint64_t nextFenceValue = 0;

	if (g_Screenshot && m_ScreenshotDelay < 0)
	{
		RGPassBuilder screenshot = graph.AddPass("Take Screenshot");
		screenshot.Bind([=](CommandContext& renderContext, const RGPassResource& resources)
			{
				D3D12_PLACED_SUBRESOURCE_FOOTPRINT textureFootprint = {};
				D3D12_RESOURCE_DESC desc = m_pTonemapTarget->GetResource()->GetDesc();
				m_pDevice->GetCopyableFootprints(&desc, 0, 1, 0, &textureFootprint, nullptr, nullptr, nullptr);
				m_pScreenshotBuffer = std::make_unique<Buffer>(this, "Screenshot Texture");			
				m_pScreenshotBuffer->Create(BufferDesc::CreateReadback(textureFootprint.Footprint.RowPitch * textureFootprint.Footprint.Height));
				renderContext.InsertResourceBarrier(m_pTonemapTarget.get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
				renderContext.InsertResourceBarrier(m_pScreenshotBuffer.get(), D3D12_RESOURCE_STATE_COPY_DEST);
				renderContext.CopyTexture(m_pTonemapTarget.get(), m_pScreenshotBuffer.get(), CD3DX12_BOX(0, 0, m_pTonemapTarget->GetWidth(), m_pTonemapTarget->GetHeight()));
				m_ScreenshotRowPitch = textureFootprint.Footprint.RowPitch;
			});
		m_ScreenshotDelay = 4;
		g_Screenshot = false;
	}

	if (m_pScreenshotBuffer)
	{
		if (m_ScreenshotDelay == 0)
		{
			TaskContext taskContext;
			TaskQueue::Execute([&](uint32_t) {
				char* pData = (char*)m_pScreenshotBuffer->Map(0, m_pScreenshotBuffer->GetSize());
				Image img;
				img.SetSize(m_pTonemapTarget->GetWidth(), m_pTonemapTarget->GetHeight(), 4);
				uint32_t imageRowPitch = m_pTonemapTarget->GetWidth() * 4;
				uint32_t targetOffset = 0;
				for (int i = 0; i < m_pTonemapTarget->GetHeight(); i++)
				{
					img.SetData(pData, targetOffset, imageRowPitch);
					pData += m_ScreenshotRowPitch;
					targetOffset += imageRowPitch;
				}
				m_pScreenshotBuffer->UnMap();

				SYSTEMTIME time;
				GetSystemTime(&time);
				char stringTarget[128];
				GetTimeFormat(LOCALE_INVARIANT, TIME_FORCE24HOURFORMAT, &time, "hh_mm_ss", stringTarget, 128);
				std::stringstream filePath;
				filePath << "Screenshot_" << stringTarget << ".jpg";
				img.Save(filePath.str().c_str());
				m_pScreenshotBuffer.reset();
			}, taskContext);
			m_ScreenshotDelay = -1;
		}
		else
		{
			m_ScreenshotDelay--;	
		}
	}

	RGPassBuilder setupLights = graph.AddPass("Setup Lights");
	sceneData.DepthStencil = setupLights.Write(sceneData.DepthStencil);
	setupLights.Bind([=](CommandContext& renderContext, const RGPassResource& resources)
		{
			DynamicAllocation allocation = renderContext.AllocateTransientMemory(m_Lights.size() * sizeof(Light));
			memcpy(allocation.pMappedMemory, m_Lights.data(), m_Lights.size() * sizeof(Light));
			renderContext.CopyBuffer(allocation.pBackingResource, m_pLightBuffer.get(), (uint32_t)m_pLightBuffer->GetSize(), (uint32_t)allocation.Offset, 0);
		});

	// depth prepass
	// - depth only pass that renders the entire scene
	// - optimization that prevents wasteful lighting calculations during the base pass
	// - required for light culling
	RGPassBuilder prepass = graph.AddPass("Depth Prepass");
	sceneData.DepthStencil = prepass.Write(sceneData.DepthStencil);
	prepass.Bind([=](CommandContext& renderContext, const RGPassResource& resources)
		{
			GraphicsTexture* pDepthStencil = resources.GetTexture(sceneData.DepthStencil);
			const TextureDesc& desc = pDepthStencil->GetDesc();

			renderContext.InsertResourceBarrier(pDepthStencil, D3D12_RESOURCE_STATE_DEPTH_WRITE);

			RenderPassInfo info = RenderPassInfo(pDepthStencil, RenderPassAccess::Clear_Store);
			renderContext.BeginRenderPass(info);
			renderContext.SetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			renderContext.SetViewport(FloatRect(0, 0, (float)desc.Width, (float)desc.Height));

			renderContext.SetPipelineState(m_pDepthPrepassPSO.get());
			renderContext.SetGraphicsRootSignature(m_pDepthPrepassRS.get());

			struct Parameters
			{
				Matrix WorldViewProj;
			} constBuffer{};

			for (const Batch& b : m_OpaqueBatches)
			{
				constBuffer.WorldViewProj = b.WorldMatrix * m_pCamera->GetViewProjection();
				renderContext.SetDynamicConstantBufferView(0, &constBuffer, sizeof(Parameters));
				b.pMesh->Draw(&renderContext);
			}

			renderContext.EndRenderPass();
		});

	// [with MSAA] depth resolve
	//  - if MSAA is enabled, run a compute shader to resolve the depth buffer
	if (m_SampleCount > 1)
	{
		RGPassBuilder depthResolve = graph.AddPass("Depth Resolve");
		sceneData.DepthStencil = depthResolve.Read(sceneData.DepthStencil);
		sceneData.DepthStencilResolved = depthResolve.Write(sceneData.DepthStencilResolved);
		depthResolve.Bind([=](CommandContext& renderContext, const RGPassResource& resources)
			{
				GraphicsTexture* pDepthStencil = resources.GetTexture(sceneData.DepthStencil);
				GraphicsTexture* pDepthStencilResolve = resources.GetTexture(sceneData.DepthStencilResolved);

				renderContext.InsertResourceBarrier(pDepthStencilResolve, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				renderContext.InsertResourceBarrier(pDepthStencil, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

				renderContext.SetComputeRootSignature(m_pResolveDepthRS.get());
				renderContext.SetPipelineState(m_pResolveDepthPSO.get());

				renderContext.SetDynamicDescriptor(0, 0, pDepthStencilResolve->GetUAV());
				renderContext.SetDynamicDescriptor(1, 0, pDepthStencil->GetSRV());

				int dispatchGroupX = Math::DivideAndRoundUp(m_WindowWidth, 16);
				int dispatchGroupY = Math::DivideAndRoundUp(m_WindowHeight, 16);
				renderContext.Dispatch(dispatchGroupX, dispatchGroupY);

				renderContext.InsertResourceBarrier(pDepthStencilResolve, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				renderContext.InsertResourceBarrier(pDepthStencil, D3D12_RESOURCE_STATE_DEPTH_READ);
				renderContext.FlushResourceBarriers();
			});
	}

	m_pGpuParticles->Simulate(graph, GetResolveDepthStencil(), *m_pCamera);

	if (g_ShowRaytraced)
	{
		m_pRTAO->Execute(graph, m_pAmbientOcclusion.get(), GetResolveDepthStencil(), *m_pCamera);
	}
	else
	{
		m_pSSAO->Execute(graph, m_pAmbientOcclusion.get(), GetResolveDepthStencil(), *m_pCamera);
	}

	// shadow mapping
	//  - renders the scene depth onto a separate depth buffer from the light's view
	if (shadowIndex > 0)
	{
		if (g_ShowSDSM)
		{
			RGPassBuilder depthReduce = graph.AddPass("Depth Reduce");
			sceneData.DepthStencil = depthReduce.Write(sceneData.DepthStencil);
			depthReduce.Bind([=](CommandContext& renderContext, const RGPassResource& resources)
				{
					GraphicsTexture* pDepthStencil = resources.GetTexture(sceneData.DepthStencil);

					renderContext.InsertResourceBarrier(pDepthStencil, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					renderContext.InsertResourceBarrier(m_ReductionTargets[0].get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

					renderContext.SetComputeRootSignature(m_pReduceDepthRS.get());
					renderContext.SetPipelineState(pDepthStencil->GetDesc().SampleCount > 1 ? m_pPrepareReduceDepthMsaaPSO.get() : m_pPrepareReduceDepthPSO.get());

					struct ShaderParameters
					{
						float Near;
						float Far;
					} parameters;
					parameters.Near = m_pCamera->GetNear();
					parameters.Far = m_pCamera->GetFar();

					renderContext.SetComputeDynamicConstantBufferView(0, &parameters, sizeof(parameters));
					renderContext.SetDynamicDescriptor(1, 0, m_ReductionTargets[0]->GetUAV());
					renderContext.SetDynamicDescriptor(2, 0, pDepthStencil->GetSRV());

					renderContext.Dispatch(m_ReductionTargets[0]->GetWidth(), m_ReductionTargets[0]->GetHeight());

					renderContext.SetPipelineState(m_pReduceDepthPSO.get());
					for (size_t i = 1; i < m_ReductionTargets.size(); i++)
					{
						renderContext.InsertResourceBarrier(m_ReductionTargets[i - 1].get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
						renderContext.InsertResourceBarrier(m_ReductionTargets[i].get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

						renderContext.SetDynamicDescriptor(1, 0, m_ReductionTargets[i]->GetUAV());
						renderContext.SetDynamicDescriptor(2, 0, m_ReductionTargets[i - 1]->GetSRV());

						renderContext.Dispatch(m_ReductionTargets[i]->GetWidth(), m_ReductionTargets[i]->GetHeight());
					}

					renderContext.InsertResourceBarrier(m_ReductionTargets.back().get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
					renderContext.FlushResourceBarriers();

					renderContext.CopyTexture(m_ReductionTargets.back().get(), m_ReductionReadbackTargets[m_Frame % FRAME_COUNT].get(), CD3DX12_BOX(0, 1));
			});
		}

		RGPassBuilder shadows = graph.AddPass("Shadow Mapping");
		shadows.Bind([=](CommandContext& context, const RGPassResource& resources)
			{
				context.InsertResourceBarrier(m_pShadowMap.get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);

				context.BeginRenderPass(RenderPassInfo(m_pShadowMap.get(), RenderPassAccess::Clear_Store));

				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				context.SetGraphicsRootSignature(m_pShadowRS.get());

				for (int i = 0; i < shadowIndex; ++i)
				{
					GPU_PROFILE_SCOPE("Light View", &context);
					const Vector4& shadowOffset = shadowData.ShadowMapOffsets[i];
					FloatRect viewport;
					viewport.Left = shadowOffset.x * (float)m_pShadowMap->GetWidth();
					viewport.Top = shadowOffset.y * (float)m_pShadowMap->GetHeight();
					viewport.Right = viewport.Left + shadowOffset.z * (float)m_pShadowMap->GetWidth();
					viewport.Bottom = viewport.Top + shadowOffset.w * (float)m_pShadowMap->GetHeight();
					context.SetViewport(viewport);

					struct PerObjectData
					{
						Matrix WorldViewProjection;
					} ObjectData{};

					{
						GPU_PROFILE_SCOPE("Opaque", &context);
						context.SetPipelineState(m_pShadowPSO.get());

						for (const Batch& b : m_OpaqueBatches)
						{
							ObjectData.WorldViewProjection = b.WorldMatrix * shadowData.LightViewProjections[i];

							context.SetDynamicConstantBufferView(0, &ObjectData, sizeof(ObjectData));
							b.pMesh->Draw(&context);
						}
					}

					{
						GPU_PROFILE_SCOPE("Transparent", &context);
						context.SetPipelineState(m_pShadowAlphaPSO.get());

						context.SetDynamicConstantBufferView(0, &ObjectData, sizeof(ObjectData));
						for (const Batch& b : m_TransparentBatches)
						{
							ObjectData.WorldViewProjection = b.WorldMatrix * shadowData.LightViewProjections[i];

							context.SetDynamicConstantBufferView(0, &ObjectData, sizeof(ObjectData));
							context.SetDynamicDescriptor(1, 0, b.pMaterial->pDiffuseTexture->GetSRV());
							b.pMesh->Draw(&context);
						}
					}
				}
				context.EndRenderPass();
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
		resources.pShadowData = &shadowData;
		resources.pAO = m_pAmbientOcclusion.get();
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
		resources.pShadowData = &shadowData;
		resources.pAO = m_pAmbientOcclusion.get();
		m_pClusteredForward->Execute(graph, resources);
	}

	m_pGpuParticles->Render(graph, GetCurrentRenderTarget(), GetDepthStencil(), *m_pCamera);
	m_pClouds->Render(graph, GetCurrentRenderTarget(), GetDepthStencil(), GetCamera());

	RGPassBuilder sky = graph.AddPass("Sky");
	sceneData.DepthStencil = sky.Read(sceneData.DepthStencil);
	sky.Bind([=](CommandContext& renderContext, const RGPassResource& inputResources)
		{
			GraphicsTexture* pDepthStencil = inputResources.GetTexture(sceneData.DepthStencil);
			const TextureDesc& desc = pDepthStencil->GetDesc();
			renderContext.InsertResourceBarrier(pDepthStencil, D3D12_RESOURCE_STATE_DEPTH_READ);
			renderContext.InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET);

			RenderPassInfo info = RenderPassInfo(GetCurrentRenderTarget(), RenderPassAccess::Load_Store, pDepthStencil, RenderPassAccess::Load_DontCare);
			
			renderContext.BeginRenderPass(info);
			renderContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			renderContext.SetViewport(FloatRect(0, 0, (float)desc.Width, (float)desc.Height));

			renderContext.SetPipelineState(m_pSkyboxPSO.get());
			renderContext.SetGraphicsRootSignature(m_pSkyboxRS.get());

			struct Parameters
			{
				Matrix View;
				Matrix Projection;
				Vector3 Bias;
				float padding1{};
				Vector3 SunDirection;
				float padding2{};
			} constBuffer;

			constBuffer.View = m_pCamera->GetView();
			constBuffer.Projection = m_pCamera->GetProjection();
			constBuffer.Bias = Vector3::One;
			constBuffer.SunDirection = -m_Lights[0].Direction;
			constBuffer.SunDirection.Normalize();

			renderContext.SetDynamicConstantBufferView(0, &constBuffer, sizeof(Parameters));

			renderContext.Draw(0, 36);

			renderContext.EndRenderPass();
		});

	DebugRenderer::Get()->Render(graph, m_pCamera->GetViewProjection(), GetCurrentRenderTarget(), GetDepthStencil());

	// MSAA render target resolve
	//  - we have to resolve a MSAA render target ourselves.
	if (m_SampleCount > 1)
	{
		RGPassBuilder resolve = graph.AddPass("Resolve MSAA");
		resolve.Bind([=](CommandContext& context, const RGPassResource& resources)
			{
				context.InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
				context.InsertResourceBarrier(m_pHDRRenderTarget.get(), D3D12_RESOURCE_STATE_RESOLVE_DEST);
				context.ResolveResource(GetCurrentRenderTarget(), 0, m_pHDRRenderTarget.get(), 0, RENDER_TARGET_FORMAT);
			});
	}

	// tonemap
	{
		bool downscaleTonemap = false;
		GraphicsTexture* pTonemapInput = downscaleTonemap ? m_pDownscaledColor.get() : m_pHDRRenderTarget.get();
		RGResourceHandle toneMappingInput = graph.ImportTexture("Tonemap Input", pTonemapInput);

		if (downscaleTonemap)
		{
			RGPassBuilder downsampleColor = graph.AddPass("Downsample Color");
			toneMappingInput = downsampleColor.Write(toneMappingInput);
			downsampleColor.Bind([=](CommandContext& context, const RGPassResource& resources)
				{
					GraphicsTexture* pTonemapInput = resources.GetTexture(toneMappingInput);
					context.InsertResourceBarrier(pTonemapInput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
					context.InsertResourceBarrier(m_pHDRRenderTarget.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

					context.SetPipelineState(m_pGenerateMipsPSO.get());
					context.SetComputeRootSignature(m_pGenerateMipsRS.get());

					struct DownscaleParameters
					{
						IntVector2 TargetDimensions;
						Vector2 TargetDimensionsInv;
					} Parameters{};

					Parameters.TargetDimensions.x = pTonemapInput->GetWidth();
					Parameters.TargetDimensions.y = pTonemapInput->GetHeight();
					Parameters.TargetDimensionsInv = Vector2(1.0f / pTonemapInput->GetWidth(), 1.0f / pTonemapInput->GetHeight());

					context.SetComputeDynamicConstantBufferView(0, &Parameters, sizeof(DownscaleParameters));
					context.SetDynamicDescriptor(1, 0, pTonemapInput->GetUAV());
					context.SetDynamicDescriptor(2, 0, m_pHDRRenderTarget->GetSRV());

					context.Dispatch(Math::DivideAndRoundUp(pTonemapInput->GetWidth(), 16), Math::DivideAndRoundUp(pTonemapInput->GetHeight(), 16));
				});
		}

		// exposure adjustment
		// luminance histogram, collect the pixel count into a 256 bins histogram with luminance
		RGPassBuilder luminanceHistogram = graph.AddPass("Luminance Histogram");
		toneMappingInput = luminanceHistogram.Read(toneMappingInput);
		luminanceHistogram.Bind([=](CommandContext& context, const RGPassResource& resources)
			{
				GraphicsTexture* pTonemapInput = resources.GetTexture(toneMappingInput);

				context.InsertResourceBarrier(m_pLuminanceHistogram.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				context.InsertResourceBarrier(pTonemapInput, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

				context.ClearUavUInt(m_pLuminanceHistogram.get(), m_pLuminanceHistogram->GetUAV());

				context.SetPipelineState(m_pLuminanceHistogramPSO.get());
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
			});

		// average luminance, compute the average luminance from the histogram
		RGPassBuilder averageLuminance = graph.AddPass("Average Luminance");
		averageLuminance.Bind([=](CommandContext& context, const RGPassResource& resources)
			{
				context.InsertResourceBarrier(m_pLuminanceHistogram.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(m_pAverageLuminance.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				context.SetPipelineState(m_pAverageLuminancePSO.get());
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
				AverageParameters.TimeDelta = Time::DeltaTime();
				AverageParameters.Tau = g_Tau;

				context.SetComputeDynamicConstantBufferView(0, &AverageParameters, sizeof(AverageLuminanceParameters));
				context.SetDynamicDescriptor(1, 0, m_pAverageLuminance->GetUAV());
				context.SetDynamicDescriptor(2, 0, m_pLuminanceHistogram->GetSRV());

				context.Dispatch(1, 1, 1);
			});

		RGPassBuilder tonemap = graph.AddPass("Tonemap");
		tonemap.Bind([=](CommandContext& context, const RGPassResource& resources)
			{
				struct Parameters
				{
					float WhitePoint;
					uint32_t ToneMapper;
				} constBuffer;
				constBuffer.WhitePoint = g_WhitePoint;
				constBuffer.ToneMapper = g_ToneMapper;

				context.InsertResourceBarrier(m_pTonemapTarget.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				context.InsertResourceBarrier(m_pAverageLuminance.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(m_pHDRRenderTarget.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

				context.SetPipelineState(m_pToneMapPSO.get());
				context.SetComputeRootSignature(m_pToneMapRS.get());

				context.SetComputeDynamicConstantBufferView(0, &constBuffer, sizeof(Parameters));
				context.SetDynamicDescriptor(1, 0, m_pTonemapTarget->GetUAV());
				context.SetDynamicDescriptor(2, 0, m_pHDRRenderTarget->GetSRV());
				context.SetDynamicDescriptor(2, 1, m_pAverageLuminance->GetSRV());
				
				context.Dispatch(Math::DivideAndRoundUp(m_pHDRRenderTarget->GetWidth(), 16), Math::DivideAndRoundUp(m_pHDRRenderTarget->GetHeight(), 16));
			});

		if (g_EnableUI && g_DrawHistogram)
		{
			RGPassBuilder drawHistogram = graph.AddPass("Draw Histogram");
			drawHistogram.Bind([=](CommandContext& context, const RGPassResource& resources)
				{
					context.InsertResourceBarrier(m_pLuminanceHistogram.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					context.InsertResourceBarrier(m_pAverageLuminance.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					context.InsertResourceBarrier(m_pTonemapTarget.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

					context.SetPipelineState(m_pDrawHistogramPSO.get());
					context.SetComputeRootSignature(m_pDrawHistogramRS.get());

					struct AverageParameters
					{
						float MinLogLuminance{0};
						float InverseLogLuminanceRange{0};
					} Parameters;

					Parameters.MinLogLuminance = g_MinLogLuminance;
					Parameters.InverseLogLuminanceRange = 1.0f / (g_MaxLogLuminance - g_MinLogLuminance);

					context.SetComputeDynamicConstantBufferView(0, &Parameters, sizeof(AverageParameters));
					context.SetDynamicDescriptor(1, 0, m_pTonemapTarget->GetUAV());
					context.SetDynamicDescriptor(2, 0, m_pLuminanceHistogram->GetSRV());
					context.SetDynamicDescriptor(2, 1, m_pAverageLuminance->GetSRV());

					context.Dispatch(1, m_pLuminanceHistogram->GetDesc().ElementCount);
				});
		}
	}

	// UI
	//  - ImGui render, pretty straight forward
	if (g_EnableUI)
	{
		m_pImGuiRenderer->Render(graph, m_pTonemapTarget.get());
	}
	else
	{
		ImGui::Render();
	}

	RGPassBuilder tempBarriers = graph.AddPass("Temp Barriers");
	tempBarriers.Bind([=](CommandContext& context, const RGPassResource& resources)
		{
			context.CopyTexture(m_pTonemapTarget.get(), GetCurrentBackbuffer());
			context.InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET);
			context.InsertResourceBarrier(GetCurrentBackbuffer(), D3D12_RESOURCE_STATE_PRESENT);
		});

	graph.Compile();
	if (g_DumpRenderGraph)
	{
		graph.DumpGraphMermaid("graph.html");
		g_DumpRenderGraph = false;
	}
	nextFenceValue = graph.Execute();

	PROFILE_END();
	// present
	//  - set fence for the currently queued frame
	//  - present the frame buffer
	//  - wait for the next frame to be finished to start queueing work for it
	EndFrame(nextFenceValue);

	if (m_CapturePix)
	{
		D3D::EndPixCapture();
		m_CapturePix = false;
	}
}

void Graphics::Shutdown()
{
	// wait for all the GPU work to finish
	IdleGPU();
	m_pSwapchain->SetFullscreenState(false, nullptr);
}

void Graphics::BeginFrame()
{
	m_pImGuiRenderer->NewFrame(m_WindowWidth, m_WindowHeight);
}

void Graphics::EndFrame(uint64_t fenceValue)
{
	Profiler::Get()->Resolve(this, m_Frame);

	// the top third(triple buffer) is not need wait, just record the every frame fenceValue.
	// the 'm_CurrentBackBufferIndex' is always in the new buffer frame
	// we present and request the new backbuffer index and wait for that one to finish on the GPU before starting to queue work for that frame

	m_FenceValues[m_CurrentBackBufferIndex] = fenceValue;
	m_pSwapchain->Present(1, 0);
	m_CurrentBackBufferIndex = m_pSwapchain->GetCurrentBackBufferIndex();
	WaitForFence(m_FenceValues[m_CurrentBackBufferIndex]);
	++m_Frame;
}

void Graphics::InitD3D()
{
	E_LOG(Info, "Graphics::InitD3D");
	UINT dxgiFactoryFlags = 0;

	bool debugD3D = CommandLine::GetBool("d3dvalidation") || D3D_VALIDATION;
	bool gpuValidation = CommandLine::GetBool("gpuvalidation") || GPU_VALIDATION;
	if (debugD3D)
	{
		ComPtr<ID3D12Debug> pDebugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pDebugController))))
		{
			pDebugController->EnableDebugLayer();  // CPU-side
		}
	
		if (gpuValidation)
		{
			ComPtr<ID3D12Debug1> pDebugController1;
			if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pDebugController1))))
			{
				pDebugController1->SetEnableGPUBasedValidation(true);  // GPU-side
			}
		}

		// additional DXGI debug layers
		dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
	}

	ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> pDredSettings;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pDredSettings))))
	{
		E_LOG(Info, "DRED Enabled");
		// turn on auto-breadcrumbs and page fault reporting
		pDredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
		pDredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
	}

	// factory
	VERIFY_HR(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_pFactory)));

	// look for an adapter
	ComPtr<IDXGIAdapter4> pAdapter;
	uint32_t adapter = 0;
	while (m_pFactory->EnumAdapterByGpuPreference(adapter++, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(pAdapter.ReleaseAndGetAddressOf())) != DXGI_ERROR_NOT_FOUND)
	{
		DXGI_ADAPTER_DESC3 desc;
		pAdapter->GetDesc3(&desc);

		if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
		{
			char name[256];
			ToMultibyte(desc.Description, name, 256);
			
			E_LOG(Info, "\t%s - %f GB", name, (float)desc.DedicatedVideoMemory * Math::ToGigaBytes);

			break;
		}
	}

	// device
	constexpr D3D_FEATURE_LEVEL featureLevels[] =
	{
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0,
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0
	};

	VERIFY_HR(D3D12CreateDevice(pAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_pDevice)));
	D3D12_FEATURE_DATA_FEATURE_LEVELS caps = {
		.NumFeatureLevels = std::size(featureLevels),
		.pFeatureLevelsRequested = featureLevels,
	};
	VERIFY_HR_EX(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &caps, sizeof(D3D12_FEATURE_DATA_FEATURE_LEVELS)), GetDevice());
	VERIFY_HR_EX(D3D12CreateDevice(pAdapter.Get(), caps.MaxSupportedFeatureLevel, IID_PPV_ARGS(m_pDevice.ReleaseAndGetAddressOf())), GetDevice());
		
	pAdapter.Reset();

	m_pDevice.As(&m_pRaytracingDevice);
	m_pDevice->SetName(L"Main Device");

	if (debugD3D)
	{
		ID3D12InfoQueue* pInfoQueue = nullptr;
		if (SUCCEEDED(m_pDevice->QueryInterface(IID_PPV_ARGS(&pInfoQueue))))
		{
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

			if (CommandLine::GetBool("d3dbreakvalidation"))
			{
				VERIFY_HR_EX(pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true), GetDevice());
			}

			pInfoQueue->PushStorageFilter(&NewFilter);
			pInfoQueue->Release();
		}
	}

	 // feature checks
	{
		D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
		if (SUCCEEDED(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS5))))
		{
			m_RenderPassTier = options.RenderPassesTier;
			m_RayTracingTier = options.RaytracingTier;
		}

		D3D12_FEATURE_DATA_SHADER_MODEL shaderModelSupport = {
			.HighestShaderModel = D3D_SHADER_MODEL_6_6
		};
		if (SUCCEEDED(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModelSupport, sizeof(shaderModelSupport))))
		{
			m_ShaderModelMajor = shaderModelSupport.HighestShaderModel >> 0x4;
			m_ShaderModelMinor = shaderModelSupport.HighestShaderModel & 0xF;

			E_LOG(Info, "D3D12 Shader Model %d.%d", m_ShaderModelMajor, m_ShaderModelMinor);
		}

		D3D12_FEATURE_DATA_D3D12_OPTIONS7 caps7{};
		if (SUCCEEDED(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &caps7, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS7))))
		{
			m_MeshShaderSupport = caps7.MeshShaderTier;
		}
	}

	// create all the required command queues
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_COMPUTE] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_COMPUTE);
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_COPY] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_COPY);
	//m_CommandQueues[D3D12_COMMAND_LIST_TYPE_BUNDLE] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_BUNDLE);

	// allocate descriptor heaps pool
	check(m_DescriptorHeaps.size() == D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES);
	m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV] = std::make_unique<OfflineDescriptorAllocator>(this, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 256);
	m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER] = std::make_unique<OfflineDescriptorAllocator>(this, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 128);
	m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_RTV] = std::make_unique<OfflineDescriptorAllocator>(this, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 128);
	m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_DSV] = std::make_unique<OfflineDescriptorAllocator>(this, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 64);

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

	m_pDynamicAllocationManager = std::make_unique<DynamicAllocationManager>(this);
	m_pResourceAllocator = std::make_unique<RGResourceAllocator>(this);

	m_pImGuiRenderer = std::make_unique<ImGuiRenderer>(this);
	m_pImGuiRenderer->AddUpdateCallback(ImGuiCallbackDelegate::CreateRaw(this, &Graphics::UpdateImGui));

	m_pHDRRenderTarget = std::make_unique<GraphicsTexture>(this, "HDR Render Target");
	m_pTonemapTarget = std::make_unique<GraphicsTexture>(this, "Tonemap Target");
	m_pDownscaledColor = std::make_unique<GraphicsTexture>(this, "Downscaled HDR Target");
	m_pAmbientOcclusion = std::make_unique<GraphicsTexture>(this, "SSAO Target");

	m_pClusteredForward = std::make_unique<ClusteredForward>(this);
	m_pTiledForward = std::make_unique<TiledForward>(this);

	m_pRTAO = std::make_unique<RTAO>(this);
	m_pSSAO = std::make_unique<SSAO>(this);
	m_pClouds = std::make_unique<Clouds>(this);
	m_pGpuParticles = std::make_unique<GpuParticles>(this);

	Profiler::Get()->Initialize(this);
	DebugRenderer::Get()->Initialize(this);

	OnResize(m_WindowWidth, m_WindowHeight);
}

void Graphics::CreateSwapchain()
{
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

	VERIFY_HR(m_pFactory->CreateSwapChainForHwnd(
		m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT]->GetCommandQueue(),
		m_pWindow,
		&swapchainDesc,
		&fsDesc,
		nullptr,
		pSwapChain.GetAddressOf()));

	m_pSwapchain.Reset();
	pSwapChain.As(&m_pSwapchain);
}

void Graphics::OnResize(int width, int height)
{
	E_LOG(Info, "Viewport resized: %dx%d", width, height);
	m_WindowWidth = width;
	m_WindowHeight = height;

	IdleGPU();

	for (int i = 0; i < FRAME_COUNT; i++)
	{
		m_Backbuffers[i]->Release();
	}
	m_pDepthStencil->Release();

	// resize the buffers
	VERIFY_HR_EX(m_pSwapchain->ResizeBuffers(
		FRAME_COUNT,
		m_WindowWidth,
		m_WindowHeight,
		SWAPCHAIN_FORMAT,
		DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH), GetDevice());

	m_CurrentBackBufferIndex = 0;

	// recreate the render target views
	for (int i = 0; i < FRAME_COUNT; i++)
	{
		ID3D12Resource* pResource = nullptr;
		VERIFY_HR_EX(m_pSwapchain->GetBuffer(i, IID_PPV_ARGS(&pResource)), GetDevice());
		m_Backbuffers[i]->CreateForSwapChain(pResource);
	}

	m_pDepthStencil->Create(TextureDesc::CreateDepth(m_WindowWidth, m_WindowHeight, DEPTH_STENCIL_FORMAT, TextureFlag::DepthStencil | TextureFlag::ShaderResource, m_SampleCount, ClearBinding(0.0f, 0)));
	if (m_SampleCount > 1)
	{
		m_pResolveDepthStencil->Create(TextureDesc::Create2D(m_WindowWidth, m_WindowHeight, DXGI_FORMAT_R32_FLOAT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess));
		m_pResolvedRenderTarget->Create(TextureDesc::CreateRenderTarget(width, height, RENDER_TARGET_FORMAT, TextureFlag::RenderTarget | TextureFlag::ShaderResource, 1, ClearBinding(Color(0, 0, 0, 0))));

		m_pMultiSampleRenderTarget->Create(TextureDesc::CreateRenderTarget(width, height, RENDER_TARGET_FORMAT, TextureFlag::RenderTarget, m_SampleCount, ClearBinding(Color(0, 0, 0, 0))));
	}

	m_pHDRRenderTarget->Create(TextureDesc::CreateRenderTarget(width, height, RENDER_TARGET_FORMAT, TextureFlag::ShaderResource | TextureFlag::RenderTarget | TextureFlag::UnorderedAccess));
	m_pTonemapTarget->Create(TextureDesc::CreateRenderTarget(width, height, SWAPCHAIN_FORMAT, TextureFlag::ShaderResource | TextureFlag::RenderTarget | TextureFlag::UnorderedAccess));
	m_pDownscaledColor->Create(TextureDesc::Create2D(Math::DivideAndRoundUp(width, 4), Math::DivideAndRoundUp(height, 4), RENDER_TARGET_FORMAT, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource));

	m_pAmbientOcclusion->Create(TextureDesc::CreateRenderTarget(Math::DivideAndRoundUp(width, 2), Math::DivideAndRoundUp(height, 2), DXGI_FORMAT_R8_UNORM, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess | TextureFlag::RenderTarget));

	m_pCamera->SetAspectRatio((float)width / height);
	m_pCamera->SetDirty();

	m_pClusteredForward->OnSwapchainCreated(width, height);
	m_pTiledForward->OnSwapchainCreated(width, height);
	m_pSSAO->OnSwapchainCreated(width, height);

	m_ReductionTargets.clear();
	int w = width;
	int h = height;
	while (w > 1 || h > 1)
	{
		w = Math::DivideAndRoundUp(w, 16);
		h = Math::DivideAndRoundUp(h, 16);
		std::unique_ptr<GraphicsTexture> pTexture = std::make_unique<GraphicsTexture>(this, "SDSM Reduction Target");
		pTexture->Create(TextureDesc::Create2D(w, h, DXGI_FORMAT_R32G32_FLOAT, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource));
		m_ReductionTargets.push_back(std::move(pTexture));
	}

	for (int i = 0; i < FRAME_COUNT; i++)
	{
		std::unique_ptr<Buffer> pBuffer = std::make_unique<Buffer>(this, "SDSM Reduction Readback Target");
		pBuffer->Create(BufferDesc::CreateTyped(1, DXGI_FORMAT_R32G32_FLOAT, BufferFlag::Readback));
		m_ReductionReadbackTargets.push_back(std::move(pBuffer));
	}
}

void Graphics::InitializeAssets(CommandContext& context)
{
	m_pMesh = std::make_unique<Mesh>();
	m_pMesh->Load("Resources/sponza/sponza.dae", this, &context);

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

	m_pRTAO->GenerateAccelerationStructure(this, m_pMesh.get(), context);
}

void Graphics::InitializePipelines()
{
	m_pLightBuffer = std::make_unique<Buffer>(this, "Light Buffer");

	// input layout
	CD3DX12_INPUT_ELEMENT_DESC depthOnlyInputElements[] = {
			CD3DX12_INPUT_ELEMENT_DESC{ "POSITION", DXGI_FORMAT_R32G32B32_FLOAT },
			CD3DX12_INPUT_ELEMENT_DESC{ "TEXCOORD", DXGI_FORMAT_R32G32B32_FLOAT },
	};

	// shadow mapping
	// vertex shader-only pass that writes to the depth buffer using the light matrix
	{

		// opaque
		Shader vertexShader("DepthOnly.hlsl", ShaderType::Vertex, "VSMain");

		// root signature
		m_pShadowRS = std::make_unique<RootSignature>();
		m_pShadowRS->FinalizeFromShader("Shadow Mapping RS", vertexShader, m_pDevice.Get());

		// pipeline state
		m_pShadowPSO = std::make_unique<PipelineState>();
		m_pShadowPSO->SetInputLayout(depthOnlyInputElements, sizeof(depthOnlyInputElements) / sizeof(depthOnlyInputElements[0]));
		m_pShadowPSO->SetRootSignature(m_pShadowRS->GetRootSignature());
		m_pShadowPSO->SetVertexShader(vertexShader);
		m_pShadowPSO->SetRenderTargetFormats(nullptr, 0, DEPTH_STENCIL_SHADOW_FORMAT, 1);
		m_pShadowPSO->SetCullMode(D3D12_CULL_MODE_NONE);
		m_pShadowPSO->SetDepthBias(-1, -5.f, -4.f);
		m_pShadowPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		m_pShadowPSO->Finalize("Shadow Mapping (Opaque) Pipeline", m_pDevice.Get());

		// transparent
		Shader alphaPixelShader("DepthOnly.hlsl", ShaderType::Pixel, "PSMain");

		m_pShadowAlphaPSO = std::make_unique<PipelineState>(*m_pShadowPSO);
		m_pShadowAlphaPSO->SetPixelShader(alphaPixelShader);
		m_pShadowAlphaPSO->Finalize("Shadow Mapping (Alpha) Pipeline", m_pDevice.Get());

		m_pShadowMap = std::make_unique<GraphicsTexture>(this, "Shadow Map");
		m_pShadowMap->Create(TextureDesc(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, DEPTH_STENCIL_SHADOW_FORMAT, TextureFlag::DepthStencil | TextureFlag::ShaderResource, 1, ClearBinding(0.0f, 0)));
	}

	// depth prepass
	// simple vertex shader to fill the depth buffer to optimize later passes
	{
		Shader vertexShader("DepthOnly.hlsl", ShaderType::Vertex, "VSMain");

		// root signature
		m_pDepthPrepassRS = std::make_unique<RootSignature>();
		m_pDepthPrepassRS->FinalizeFromShader("Depth Prepass RS", vertexShader, m_pDevice.Get());

		// pipeline state
		m_pDepthPrepassPSO = std::make_unique<PipelineState>();
		m_pDepthPrepassPSO->SetInputLayout(depthOnlyInputElements, std::size(depthOnlyInputElements));
		m_pDepthPrepassPSO->SetRootSignature(m_pDepthPrepassRS->GetRootSignature());
		m_pDepthPrepassPSO->SetVertexShader(vertexShader);
		m_pDepthPrepassPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		m_pDepthPrepassPSO->SetRenderTargetFormats(nullptr, 0, DEPTH_STENCIL_FORMAT, m_SampleCount);
		m_pDepthPrepassPSO->Finalize("Depth Prepass PSO", m_pDevice.Get());
	}

	// luminance histogram
	{
		Shader computeShader("Tonemap/LuminanceHistogram.hlsl", ShaderType::Compute, "CSMain");

		// root signature
		m_pLuminanceHistogramRS = std::make_unique<RootSignature>();
		m_pLuminanceHistogramRS->FinalizeFromShader("Luminance Histogram RS", computeShader, m_pDevice.Get());

		// pipeline state
		m_pLuminanceHistogramPSO = std::make_unique<PipelineState>();
		m_pLuminanceHistogramPSO->SetComputeShader(computeShader);
		m_pLuminanceHistogramPSO->SetRootSignature(m_pLuminanceHistogramRS->GetRootSignature());
		m_pLuminanceHistogramPSO->Finalize("Luminance Histogram PSO", m_pDevice.Get());

		m_pLuminanceHistogram = std::make_unique<Buffer>(this, "Luminance Histogram");
		m_pLuminanceHistogram->Create(BufferDesc::CreateByteAddress(sizeof(uint32_t) * 256));
		m_pAverageLuminance = std::make_unique<Buffer>(this, "Average Luminance");
		m_pAverageLuminance->Create(BufferDesc::CreateStructured(3, sizeof(float), BufferFlag::UnorderedAccess | BufferFlag::ShaderResource));
	}

	// draw histogram
	{
		Shader computeShader("Tonemap/DrawLuminanceHistogram.hlsl", ShaderType::Compute, "DrawLuminanceHistogram");
		m_pDrawHistogramRS = std::make_unique<RootSignature>();
		m_pDrawHistogramRS->FinalizeFromShader("Draw Histogram RS", computeShader, m_pDevice.Get());

		m_pDrawHistogramPSO = std::make_unique<PipelineState>();
		m_pDrawHistogramPSO->SetComputeShader(computeShader);
		m_pDrawHistogramPSO->SetRootSignature(m_pDrawHistogramRS->GetRootSignature());
		m_pDrawHistogramPSO->Finalize("Draw Histogram PSO", m_pDevice.Get());
	}

	// average luminance
	{
		Shader computeShader("Tonemap/AverageLuminance.hlsl", ShaderType::Compute, "CSMain");

		// root signature
		m_pAverageLuminanceRS = std::make_unique<RootSignature>();
		m_pAverageLuminanceRS->FinalizeFromShader("Average Luminance RS", computeShader, m_pDevice.Get());

		// pipeline state
		m_pAverageLuminancePSO = std::make_unique<PipelineState>();
		m_pAverageLuminancePSO->SetComputeShader(computeShader);
		m_pAverageLuminancePSO->SetRootSignature(m_pAverageLuminanceRS->GetRootSignature());
		m_pAverageLuminancePSO->Finalize("Average Luminance PSO", m_pDevice.Get());
	}

	// tonemapping
	{
		Shader computeShader("Tonemap/Tonemapping.hlsl", ShaderType::Compute, "CSMain");

		// rootSignature
		m_pToneMapRS = std::make_unique<RootSignature>();
		m_pToneMapRS->FinalizeFromShader("Tonemapping RS", computeShader, m_pDevice.Get());

		// pipeline state
		m_pToneMapPSO = std::make_unique<PipelineState>();
		m_pToneMapPSO->SetRootSignature(m_pToneMapRS->GetRootSignature());
		m_pToneMapPSO->SetComputeShader(computeShader);
		m_pToneMapPSO->Finalize("Tonemapping PSO", m_pDevice.Get());
	}

	// depth resolve
	// resolves a multisampled buffer to a normal depth buffer
	// only required when the sample count > 1
	{
		Shader computeShader("ResolveDepth.hlsl", ShaderType::Compute, "CSMain");

		m_pResolveDepthRS = std::make_unique<RootSignature>();
		m_pResolveDepthRS->FinalizeFromShader("Resolve Depth RS", computeShader, m_pDevice.Get());

		m_pResolveDepthPSO = std::make_unique<PipelineState>();
		m_pResolveDepthPSO->SetComputeShader(computeShader);
		m_pResolveDepthPSO->SetRootSignature(m_pResolveDepthRS->GetRootSignature());
		m_pResolveDepthPSO->Finalize("Resolve Depth Pipeline", m_pDevice.Get());
	}

	// depth reduce
	{
		Shader prepareReduceShader("ReduceDepth.hlsl", ShaderType::Compute, "PrepareReduceDepth");
		Shader prepareReduceShaderMSAA("ReduceDepth.hlsl", ShaderType::Compute, "PrepareReduceDepth", { "WITH_MSAA" });
		Shader reduceShader("ReduceDepth.hlsl", ShaderType::Compute, "ReduceDepth");

		m_pReduceDepthRS = std::make_unique<RootSignature>();
		m_pReduceDepthRS->FinalizeFromShader("Reduce Depth RS", reduceShader, m_pDevice.Get());

		m_pPrepareReduceDepthPSO = std::make_unique<PipelineState>();
		m_pPrepareReduceDepthPSO->SetComputeShader(prepareReduceShader);
		m_pPrepareReduceDepthPSO->SetRootSignature(m_pReduceDepthRS->GetRootSignature());
		m_pPrepareReduceDepthPSO->Finalize("Prepare Reduce Depth PSO", m_pDevice.Get());

		m_pPrepareReduceDepthMsaaPSO = std::make_unique<PipelineState>(*m_pPrepareReduceDepthPSO);
		m_pPrepareReduceDepthMsaaPSO->SetComputeShader(prepareReduceShaderMSAA);
		m_pPrepareReduceDepthMsaaPSO->Finalize("Prepare Reduce Depth MSAA PSO", m_pDevice.Get());

		m_pReduceDepthPSO = std::make_unique<PipelineState>(*m_pPrepareReduceDepthPSO);
		m_pReduceDepthPSO->SetComputeShader(reduceShader);
		m_pReduceDepthPSO->Finalize("Reduce Depth PSO", m_pDevice.Get());
	}

	// mip generation
	{
		Shader computeShader("GenerateMips.hlsl", ShaderType::Compute, "CSMain");

		m_pGenerateMipsRS = std::make_unique<RootSignature>();
		m_pGenerateMipsRS->FinalizeFromShader("Generate Mips RS", computeShader, m_pDevice.Get());

		m_pGenerateMipsPSO = std::make_unique<PipelineState>();
		m_pGenerateMipsPSO->SetComputeShader(computeShader);
		m_pGenerateMipsPSO->SetRootSignature(m_pGenerateMipsRS->GetRootSignature());
		m_pGenerateMipsPSO->Finalize("Generate Mips PSO", m_pDevice.Get());
	}

	// sky
	{
		Shader vertexShader("ProceduralSky.hlsl", ShaderType::Vertex, "VSMain");
		Shader pixelShader("ProceduralSky.hlsl", ShaderType::Pixel, "PSMain");

		// root signature
		m_pSkyboxRS = std::make_unique<RootSignature>();
		m_pSkyboxRS->FinalizeFromShader("Skybox RS", vertexShader, m_pDevice.Get());

		// pipeline state
		m_pSkyboxPSO = std::make_unique<PipelineState>();
		m_pSkyboxPSO->SetInputLayout(nullptr, 0);
		m_pSkyboxPSO->SetRootSignature(m_pSkyboxRS->GetRootSignature());
		m_pSkyboxPSO->SetVertexShader(vertexShader);
		m_pSkyboxPSO->SetPixelShader(pixelShader);
		m_pSkyboxPSO->SetRenderTargetFormat(RENDER_TARGET_FORMAT, DEPTH_STENCIL_FORMAT, m_SampleCount);
		m_pSkyboxPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		m_pSkyboxPSO->Finalize("Skybox", m_pDevice.Get());
	}
}

void Graphics::UpdateImGui()
{
	m_FrameTimes[m_Frame % m_FrameTimes.size()] = Time::DeltaTime();

	ImGui::SetNextWindowPos(ImVec2(0, 0), 0, ImVec2(0, 0));
	ImGui::SetNextWindowSize(ImVec2(300, (float)m_WindowHeight));
	ImGui::Begin("GPU Stats", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	ImGui::Text("MS: %4.2f", Time::DeltaTime() * 1000.0f);
	ImGui::SameLine(100);
	ImGui::Text("%d x %d", m_WindowWidth, m_WindowHeight);
	ImGui::SameLine(180.0f);
	ImGui::Text("%dx MSAA", m_SampleCount);
	ImGui::PlotLines("##", m_FrameTimes.data(), (int)m_FrameTimes.size(), m_Frame % m_FrameTimes.size(), 0, 0.0f, 0.03f, ImVec2(ImGui::GetContentRegionAvail().x, 100));

	static int currentItemIndex = 0;
	const char* items[] = { "AO", "ShadowMap" };
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

		if (m_RenderPath == RenderPath::Clustered)
		{
			extern bool g_VisualizeClusters;
			ImGui::Checkbox("Visualize Clusters", &g_VisualizeClusters);
		}
		else if (m_RenderPath == RenderPath::Tiled)
		{
			extern bool g_VisualizeLightDensity;
			ImGui::Checkbox("Visualize Light Density", &g_VisualizeLightDensity);
		}

		ImGui::Separator();
		ImGui::SliderInt("Lights", &m_DesiredLightCount, 10, 10000);
		if (ImGui::Button("Generate Lights"))
		{
			RandomizeLights(m_DesiredLightCount);
		}

		if (ImGui::Button("Dump RenderGraph"))
		{
			g_DumpRenderGraph = true;
		}
		if (ImGui::Button("Screenshot"))
		{
			g_Screenshot = true;
		}
		if (ImGui::Button("Pix Capture"))
		{
			m_CapturePix = true;
		}
	
		if (ImGui::BeginCombo("DebugTexture", items[currentItemIndex]))
		{
			for (int n = 0; n < IM_ARRAYSIZE(items); n++)
			{
				bool isSelected = (currentItemIndex == n);
				if (ImGui::Selectable(items[n], isSelected))
				{
					currentItemIndex = n;
				}

				if (isSelected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
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
	if (ImGui::TreeNodeEx("Memory", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Text("Dynamic Upload Memory");
		ImGui::Text("%.2f MB", Math::ToMegaBytes * m_pDynamicAllocationManager->GetMemoryUsage());
		ImGui::TreePop();
	}

	ImGui::End();

	static bool showOutputLog = false;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
	ImGui::SetNextWindowPos(ImVec2(300, showOutputLog ? (float)m_WindowHeight - 250 : (float)m_WindowHeight - 20));
	ImGui::SetNextWindowSize(ImVec2(showOutputLog ? (float)(m_WindowWidth - 300) * 0.5f : (float)m_WindowWidth - 250, 250));
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
		ImGui::SetNextWindowPos(ImVec2(300 + (m_WindowWidth - 300) * 0.5f, (float)(showOutputLog ? m_WindowHeight - 250 : m_WindowHeight - 20)));
		ImGui::SetNextWindowSize(ImVec2((m_WindowWidth - 300) * 0.5f, 250));
		ImGui::SetNextWindowCollapsed(!showOutputLog);
		ImGui::Begin("Profiler", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
		ProfileNode* pRootNode = Profiler::Get()->GetRootNode();
		pRootNode->RenderImGui(m_Frame);
		ImGui::End();
	}
	ImGui::PopStyleVar();

	m_pVisualizeTexture = nullptr;
	std::string title = "";
	switch (currentItemIndex)
	{
	case 0:
	{
		m_pVisualizeTexture = m_pAmbientOcclusion.get();
		title = "Ambient Occlusion: " + std::string(g_ShowRaytraced ? "RTAO" : "SSAO");
	}
	break;
	case 1:
	{
		m_pVisualizeTexture = m_pShadowMap.get();
		title = "ShadowMap";
	}
	break;
	default:
		break;
	}

	if (m_pVisualizeTexture)
	{
		ImGui::SetNextWindowPos(ImVec2(300, 0), 0, ImVec2(0, 0));
		ImGui::Begin(title.c_str());
		Vector2 image((float)m_pVisualizeTexture->GetWidth(), (float)m_pVisualizeTexture->GetHeight());
		Vector2 windowSize(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);
		float width = windowSize.x;
		float height = windowSize.x * image.y / image.x;
		if (image.x / windowSize.x < image.y / windowSize.y)
		{
			width = image.x / image.y * windowSize.y;
			height = windowSize.y;
		}

		ImTextureID user_texture_id = m_pVisualizeTexture->GetSRV().ptr;
		ImGui::Image(user_texture_id, ImVec2(width, height));
		ImGui::End();
	}
	
	ImGui::SetNextWindowPos(ImVec2(300, 20), 0, ImVec2(0, 0));
	ImGui::Begin("Parameters");

	ImGui::Text("Sky");
	ImGui::SliderFloat("Sun Orientation", &g_SunOrientation, -Math::PI, Math::PI);
	ImGui::SliderFloat("Sun Inclination", &g_SunInclination, 0.001f, 1);
	ImGui::SliderFloat("Sun Temperature", &g_SunTemperature, 1000, 15000);

	ImGui::Text("Shadows");
	ImGui::Checkbox("SDSM", &g_ShowSDSM);
	ImGui::Checkbox("Stabilize Cascades", &g_StabilizeCascases);
	ImGui::SliderFloat("PSSM Factor", &g_PSSMFactor, 0, 1);
	
	ImGui::Text("Expose/Tonemapping");
	ImGui::SliderFloat("Min Log Luminance", &g_MinLogLuminance, -100, 20);
	ImGui::SliderFloat("Max Log Luminance", &g_MaxLogLuminance, -50, 50);
	ImGui::Checkbox("Draw Exposure Histogram", &g_DrawHistogram);
	ImGui::SliderFloat("White Point", &g_WhitePoint, 0, 20);
	ImGui::Combo("Tonemapper", (int*)&g_ToneMapper, [](void* data, int index, const char** outText)
		{
			switch (index)
			{
			case 0:
				*outText = "Reinhard";
				break;
			case 1:
				*outText = "Reinhard Extended";
				break;
			case 2:
				*outText = "ACES fast";
				break;
			case 3:
				*outText = "Unreal 3";
				break;
			case 4:
				*outText = "Uncharted2";
				break;
			default:
				return false;
				break;
			}
			return true;
		}, nullptr, 5);
	ImGui::SliderFloat("Tau", &g_Tau, 0, 5);

	ImGui::Text("Misc");
	ImGui::Checkbox("Visualize Lights", &g_VisualizeLights);

	if (ImGui::Checkbox("Raytracing", &g_ShowRaytraced))
	{
		if (m_RayTracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
		{
			g_ShowRaytraced = false;
		}
	}

	ImGui::End();
}

void Graphics::RandomizeLights(int count)
{
	m_Lights.resize(count);

	BoundingBox sceneBounds;
	sceneBounds.Center = Vector3(0, 50, 0);
	sceneBounds.Extents = Vector3(140, 70, 60);

	Vector3 position(-150, 160, -10);
	Vector3 direction;
	position.Normalize(direction);

	m_Lights[0] = Light::Directional(position, -direction, 5.0f);
	m_Lights[0].ShadowIndex = 0;
	
	m_Lights[1] = Light::Point(Vector3(0, 10, 0), 200, 5000, Color(1, 0.2f, 0.2f, 1));
	m_Lights[1].ShadowIndex = 0;

	m_Lights[2] = Light::Spot(Vector3(0, 10, -10), 200, Vector3(0, 0, 1), 90, 70, 5000, Color(1, 0, 0, 1.0f));
	m_Lights[2].ShadowIndex = 0;

	if (m_pLightBuffer->GetDesc().ElementCount != m_Lights.size())
	{
		IdleGPU();
		m_pLightBuffer->Create(BufferDesc::CreateStructured((uint32_t)m_Lights.size(), sizeof(Light), BufferFlag::ShaderResource));
	}
}

CommandQueue* Graphics::GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const
{
	return m_CommandQueues.at(type).get();
}

CommandContext* Graphics::AllocateCommandContext(D3D12_COMMAND_LIST_TYPE type)
{
	int typeIndex = type;
	bool isNew = false;
	CommandContext* pCommandContext = nullptr;

	{
		std::scoped_lock lockGuard(m_ContextAllocationMutex);

		if (m_FreeCommandContexts[typeIndex].size() > 0)
		{
			pCommandContext = m_FreeCommandContexts[typeIndex].front();
			m_FreeCommandContexts[typeIndex].pop();
		}
		else
		{
			ComPtr<ID3D12CommandList> pCommandList;
			ID3D12CommandAllocator* pAllocator = m_CommandQueues[type]->RequestAllocator();
			m_pDevice->CreateCommandList(0, type, pAllocator, nullptr, IID_PPV_ARGS(&pCommandList));
			m_CommandLists.push_back(std::move(pCommandList));
			m_CommandListPool[typeIndex].emplace_back(std::make_unique<CommandContext>(this, static_cast<ID3D12GraphicsCommandList*>(m_CommandLists.back().Get()), pAllocator, type));
			pCommandContext = m_CommandListPool[typeIndex].back().get();
			isNew = true;
		}
	}

	if (!isNew)
	{
		pCommandContext->Reset();
	}

	return pCommandContext;
}

void Graphics::WaitForFence(uint64_t fenceValue)
{
	D3D12_COMMAND_LIST_TYPE type = (D3D12_COMMAND_LIST_TYPE)(fenceValue >> 56);
	CommandQueue* pQueue = GetCommandQueue(type);
	pQueue->WaitForFence(fenceValue);
}

void Graphics::FreeCommandList(CommandContext* pCommandContext)
{
	std::scoped_lock lockGuard(m_ContextAllocationMutex);
	m_FreeCommandContexts[pCommandContext->GetType()].push(pCommandContext);
}

bool Graphics::GetShaderModel(int& major, int& minor) const
{
	bool supported = m_ShaderModelMajor > major || (m_ShaderModelMajor == major && m_ShaderModelMinor >= minor);
	major = m_ShaderModelMajor;
	minor = m_ShaderModelMinor;
	return supported;
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

bool Graphics::CheckTypedUAVSupport(DXGI_FORMAT format) const
{
	D3D12_FEATURE_DATA_D3D12_OPTIONS featureData{};
	VERIFY_HR_EX(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &featureData, sizeof(featureData)), GetDevice());

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
			VERIFY_HR_EX(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &formatSupport, sizeof(formatSupport)), GetDevice());
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

uint32_t Graphics::GetMaxMSAAQuality(uint32_t msaa, DXGI_FORMAT format)
{
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qualityLevels;
	qualityLevels.Format = format == DXGI_FORMAT_UNKNOWN ? RENDER_TARGET_FORMAT : format;
	qualityLevels.SampleCount = msaa;
	qualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	VERIFY_HR_EX(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &qualityLevels, sizeof(qualityLevels)), GetDevice());
	return qualityLevels.NumQualityLevels - 1;
}

ID3D12Resource* Graphics::CreateResource(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType, D3D12_CLEAR_VALUE* pClearValue)
{
	ID3D12Resource* pResource;

	D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(heapType);
	VERIFY_HR_EX(m_pDevice->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		initialState,
		pClearValue,
		IID_PPV_ARGS(&pResource)), GetDevice());

	return pResource;
}

