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
#include "Graphics/Techniques/RTReflections.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/Blackboard.h"
#include "Core/CommandLine.h"
#include "Core/TaskQueue.h"
#include "Content/image.h"

#ifndef D3D_VALIDATION
#define D3D_VALIDATION 0
#endif

#ifndef GPU_VALIDATION
#define GPU_VALIDATION 0
#endif

namespace Tweakables
{
	bool g_DumpRenderGraph = false;
	bool g_Screenshot = false;

	float g_WhitePoint = 1;
	float g_MinLogLuminance = -10.0f;
	float g_MaxLogLuminance = 20.0f;
	float g_Tau = 2;

	bool g_DrawHistogram = false;
	uint32_t g_ToneMapper = 2;

	bool g_ShowSDSM = false;
	bool g_StabilizeCascases = true;
	bool g_VisualizeShadowCascades = false;
	int g_ShadowCascades = 4;
	float g_PSSMFactor = 1.0f;

	bool g_RaytracedAO = false;
	bool g_RaytracedReflections = false;

	bool g_VisualizeLights = false;
	bool g_VisualizeLightDensity = false;

	bool g_TAA = true;
	bool g_TestTAA = true;

	float g_SunInclination = 0.2f;
	float g_SunOrientation = -3.055f;
	float g_SunTemperature = 5000.0f;
	float g_SunIntensity = 3.0f;

	int g_SsrSamples = 16;

	int g_ShadowMapIndex = 0;

	bool g_EnableUI = true;
}

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

	m_pDynamicAllocationManager->CollectGrabage();
}

void Graphics::Update()
{
	PROFILE_BEGIN("Update");
	BeginFrame();
	m_pImGuiRenderer->Update();

	PROFILE_BEGIN("UpdateGameState");

	/*Vector3 pos = m_pCamera->GetPosition();
	pos.z = 0;
	pos.y = 50;
	pos.x = 60 * sin(Time::TotalTime());
	m_pCamera->SetPosition(pos);
	m_pCamera->SetRotation(Quaternion::CreateFromYawPitchRoll(Math::PIDIV2, 0, 0));*/
#if 1
	Vector3 pos = m_pCamera->GetPosition();
	pos.x = 48;
	pos.y = sin(5 * Time::TotalTime()) * 4 + 84;
	pos.x = -2.6f;
	m_pCamera->SetPosition(pos);
#endif

	m_pCamera->Update();

	float costheta = cosf(Tweakables::g_SunOrientation);
	float sintheta = sinf(Tweakables::g_SunOrientation);
	float cosphi = cosf(Tweakables::g_SunInclination * Math::PIDIV2);
	float sinphi = sinf(Tweakables::g_SunInclination * Math::PIDIV2);
	m_Lights[0].Direction = -Vector3(costheta * sinphi, cosphi, sintheta * sinphi);
	m_Lights[0].Colour = Math::EncodeColor(Math::MakeFromColorTemperature(Tweakables::g_SunTemperature));
	m_Lights[0].Intensity = Tweakables::g_SunIntensity;

	std::sort(m_SceneData.TransparentBatches.begin(), m_SceneData.TransparentBatches.end(), [this](const Batch& a, const Batch& b) {
		float aDist = Vector3::DistanceSquared(a.pMesh->GetBounds().Center, m_pCamera->GetPosition());
		float bDist = Vector3::DistanceSquared(b.pMesh->GetBounds().Center, m_pCamera->GetPosition());
		return aDist > bDist;
		});

	std::sort(m_SceneData.OpaqueBatches.begin(), m_SceneData.OpaqueBatches.end(), [this](const Batch& a, const Batch& b) {
		float aDist = Vector3::DistanceSquared(a.pMesh->GetBounds().Center, m_pCamera->GetPosition());
		float bDist = Vector3::DistanceSquared(b.pMesh->GetBounds().Center, m_pCamera->GetPosition());
		return aDist < bDist;
		});

	//m_Lights[1].Position.x = 50 * sin(Time::TotalTime());

	if (Tweakables::g_VisualizeLights)
	{
		for (const auto& light : m_Lights)
		{
			DebugRenderer::Get()->AddLight(light);
		}
	}

	if (Input::Instance().IsKeyPressed('U'))
	{
		Tweakables::g_EnableUI = !Tweakables::g_EnableUI;
	}

	// shadow map partitioning
	//////////////////////////////////

	ShadowData shadowData;
	float minPoint = 0;
	float maxPoint = 1;
	constexpr uint32_t MAX_CASCADES = 4;
	std::array<float, MAX_CASCADES> cascadeSplits{};

	shadowData.NumCascades = Tweakables::g_ShadowCascades;

	if (Tweakables::g_ShowSDSM)
	{
		Buffer* pSourceBuffer = m_ReductionReadbackTargets[(m_Frame + 1) % FRAME_COUNT].get();
		Vector2* pData = (Vector2*)pSourceBuffer->Map();
		minPoint = pData->x;
		maxPoint = pData->y;
		pSourceBuffer->UnMap();
	}

	float n = m_pCamera->GetNear();
	float f = m_pCamera->GetFar();
	float nearPlane = Math::Min(n, f);
	float farPlane = Math::Max(n, f);

	float clipPlaneRange = farPlane - nearPlane;

	float minZ = nearPlane + minPoint * clipPlaneRange;
	float maxZ = nearPlane + maxPoint * clipPlaneRange;

	for (int i = 0; i < Tweakables::g_ShadowCascades; i++)
	{
		float p = (i + 1) / (float)Tweakables::g_ShadowCascades;
		float log = minZ * std::pow(maxZ / minZ, p);
		float uniform = minZ + (maxZ - minZ) * p;
		float d = Tweakables::g_PSSMFactor * (log - uniform) + uniform;
		cascadeSplits[i] = (d - nearPlane) / clipPlaneRange;
	}

	int shadowIndex = 0;
	for (size_t i = 0; i < m_Lights.size(); ++i)
	{
		Light& light = m_Lights[i];
		if (!light.CastShadows)
		{
			continue;
		}
		light.ShadowIndex = shadowIndex;
		if (light.Type == LightType::Directional)
		{
			for (int cascadeIdx = 0; cascadeIdx < Tweakables::g_ShadowCascades; ++cascadeIdx)
			{
				float previousCascadeSplit = cascadeIdx == 0 ? minPoint : cascadeSplits[cascadeIdx - 1];
				float currentCascadeSplit = cascadeSplits[cascadeIdx];

				Vector3 frustumCorners[] = {
					// near
					Vector3(-1, -1, 1),
					Vector3(-1,  1, 1),
					Vector3(1,  1, 1),
					Vector3(1, -1, 1),
					// far
					Vector3(-1, -1, 0),
					Vector3(-1,  1, 0),
					Vector3(1,  1, 0),
					Vector3(1, -1, 0),
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
				if (Tweakables::g_StabilizeCascases)
				{
					float radius = 0;
					for (const Vector3& corner : frustumCorners)
					{
						float distS = Vector3::DistanceSquared(corner, center);
						radius = Math::Max(radius, distS);
					}
					radius = std::sqrt(radius);
					maxExtents = Vector3(radius);
					minExtents = -maxExtents;
				}
				else
				{
					Matrix lightView = Math::CreateLookToMatrix(center, m_Lights[0].Direction, Vector3::Up);
					for (const Vector3& corner : frustumCorners)
					{
						Vector3 transformedCorner = Vector3::Transform(corner, lightView);
						minExtents = Vector3::Min(minExtents, transformedCorner);
						maxExtents = Vector3::Max(maxExtents, transformedCorner);
					}
				}

				Matrix shadowView = Math::CreateLookToMatrix(center + light.Direction * -400, light.Direction, Vector3::Up);
				Matrix projectionMatrix = Math::CreateOrthographicOffCenterMatrix(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, maxExtents.z + 400, 0);
				Matrix lightViewProjection = shadowView * projectionMatrix;

				// snap projection to shadowmap texels to avoid flickering edges
				if (Tweakables::g_StabilizeCascases)
				{
					float shadowMapSize = 2048;
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

				shadowData.CascadeDepths[shadowIndex] = currentCascadeSplit * (farPlane - nearPlane) + nearPlane;
				shadowData.LightViewProjections[shadowIndex++] = lightViewProjection;
			}
		}
		else if (light.Type == LightType::Spot)
		{
			Matrix projection = Math::CreatePerspectiveMatrix(light.UmbraAngle * Math::ToRadians, 1.0f, light.Range, 1.0f);
			shadowData.LightViewProjections[shadowIndex++] = Math::CreateLookToMatrix(light.Position, light.Direction, Vector3::Up) * projection;
		}
		else if (light.Type == LightType::Point)
		{
			Matrix projection = Math::CreatePerspectiveMatrix(Math::PIDIV2, 1, light.Range, 1.0f);

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
				shadowData.LightViewProjections[shadowIndex] = Matrix::CreateLookAt(light.Position, light.Position + cubemapDirections[j], cubemapDirections[j]) * projection;
				++shadowIndex;
			}
		}
	}

	if (shadowIndex > m_ShadowMaps.size())
	{
		m_ShadowMaps.resize(shadowIndex);
		int i = 0;
		for (auto& pShadowMap : m_ShadowMaps)
		{
			int size = (i < 4) ? 2048 : 512;
			pShadowMap = std::make_unique<GraphicsTexture>(this, "Shadow Map");
			pShadowMap->Create(TextureDesc::CreateDepth(size, size, DEPTH_STENCIL_SHADOW_FORMAT, TextureFlag::DepthStencil | TextureFlag::ShaderResource, 1, ClearBinding(0.0f, 0)));
			++i;
		}
	}

	for (Light& light : m_Lights)
	{
		if (light.ShadowIndex >= 0)
		{
			light.ShadowMapSize = m_ShadowMaps[light.ShadowIndex]->GetWidth();
		}
	}

	m_SceneData.pDepthBuffer = GetDepthStencil();
	m_SceneData.pResolvedDepth = GetResolveDepthStencil();
	m_SceneData.pShadowMaps = &m_ShadowMaps;
	m_SceneData.pRenderTarget = GetCurrentRenderTarget();
	m_SceneData.pPreviousColor = m_pPreviousColor.get();
	m_SceneData.pAO = m_pAmbientOcclusion.get();
	m_SceneData.pLightBuffer = m_pLightBuffer.get();
	m_SceneData.pCamera = m_pCamera.get();
	m_SceneData.pShadowData = &shadowData;
	m_SceneData.FrameIndex = m_Frame;
	m_SceneData.pTLAS = m_pTLAS.get();
	m_SceneData.pMesh = m_pMesh.get();

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

	if (Tweakables::g_Screenshot && m_ScreenshotDelay < 0)
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
		Tweakables::g_Screenshot = false;
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

				char filePath[256];
				sprintf_s(filePath, "Screenshot_%1s.jpg", stringTarget);
				img.Save(filePath);

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
			DynamicAllocation allocation = renderContext.AllocateTransientMemory(m_Lights.size() * sizeof(Light::RenderData));
			Light::RenderData* pTarget = (Light::RenderData*)allocation.pMappedMemory;
			for (const Light& light : m_Lights)
			{
				*pTarget = light.GetData();
				++pTarget;
			}
			renderContext.InsertResourceBarrier(m_pLightBuffer.get(), D3D12_RESOURCE_STATE_COPY_DEST);
			renderContext.FlushResourceBarriers();
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

			renderContext.InsertResourceBarrier(pDepthStencil, D3D12_RESOURCE_STATE_DEPTH_WRITE);

			RenderPassInfo info = RenderPassInfo(pDepthStencil, RenderPassAccess::Clear_Store);
			renderContext.BeginRenderPass(info);
			renderContext.SetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			renderContext.SetPipelineState(m_pDepthPrepassPSO.get());
			renderContext.SetGraphicsRootSignature(m_pDepthPrepassRS.get());

			struct ViewData
			{
				Matrix ViewProjection;
			} viewData{};
			viewData.ViewProjection = m_pCamera->GetViewProjection();
			renderContext.SetDynamicConstantBufferView(1, &viewData, sizeof(ViewData));

			for (const Batch& b : m_SceneData.OpaqueBatches)
			{
				struct ObjectData
				{
					Matrix World;
				} objectData;
				objectData.World = b.WorldMatrix;
				renderContext.SetDynamicConstantBufferView(0, &objectData, sizeof(ObjectData));

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
	else
	{
		RGPassBuilder depthResolve = graph.AddPass("Depth Resolve");
		depthResolve.Bind([=](CommandContext& renderContext, const RGPassResource& resources)
			{
				renderContext.CopyTexture(GetDepthStencil(), GetResolveDepthStencil());
			});
	}

	// camera velocity
	if (Tweakables::g_TAA)
	{
		RGPassBuilder cameraMotion = graph.AddPass("Camera Motion");
		cameraMotion.Bind([=](CommandContext& renderContext, const RGPassResource& resources)
			{
				renderContext.InsertResourceBarrier(GetResolveDepthStencil(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				renderContext.InsertResourceBarrier(m_pVelocity.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				renderContext.SetComputeRootSignature(m_pCameraMotionRS.get());
				renderContext.SetPipelineState(m_pCameraMotionPSO.get());

				struct Parameters
				{
					Matrix ReprojectionMatrix;
					Vector2 InvScreenDimensions;
				} parameters;

				Matrix preMult = Matrix(
					Vector4(2.0f, 0.0f, 0.0f, 0.0f),
					Vector4(0.0f, -2.0f, 0.0f, 0.0f),
					Vector4(0.0f, 0.0f, 1.0f, 0.0f),
					Vector4(-1.0f, 1.0f, 0.0f, 1.0f)
				);

				Matrix postMult = Matrix(
					Vector4(1.0f / 2.0f, 0.0f, 0.0f, 0.0f),
					Vector4(0.0f, -1.0f / 2.0f, 0.0f, 0.0f),
					Vector4(0.0f, 0.0f, 1.0f, 0.0f),
					Vector4(1.0f / 2.0f, 1.0f / 2.0f, 0.0f, 1.0f)
				);

				parameters.ReprojectionMatrix = preMult * m_pCamera->GetViewProjection().Invert() * m_pCamera->GetPreviousViewProjection() * postMult;
				parameters.InvScreenDimensions = Vector2(1.0f / m_WindowWidth, 1.0f / m_WindowHeight);

				renderContext.SetComputeDynamicConstantBufferView(0, &parameters, sizeof(Parameters));

				renderContext.SetDynamicDescriptor(1, 0, m_pVelocity->GetUAV());
				renderContext.SetDynamicDescriptor(2, 0, GetResolveDepthStencil()->GetSRV());

				int dispatchGroupX = Math::DivideAndRoundUp(m_WindowWidth, 8);
				int dispatchGroupY = Math::DivideAndRoundUp(m_WindowHeight, 8);
				renderContext.Dispatch(dispatchGroupX, dispatchGroupY);
			});
	}

	m_pGpuParticles->Simulate(graph, GetResolveDepthStencil(), *m_pCamera);

	if (Tweakables::g_RaytracedAO)
	{
		m_pRTAO->Execute(graph, m_pAmbientOcclusion.get(), GetResolveDepthStencil(), m_pTLAS.get(), *m_pCamera);
	}
	else
	{
		m_pSSAO->Execute(graph, m_pAmbientOcclusion.get(), GetResolveDepthStencil(), *m_pCamera);
	}

	if (Tweakables::g_RaytracedReflections)
	{
		m_SceneData.pReflection = m_pRTReflections->Execute(graph, m_SceneData);
	}

	// shadow mapping
	//  - renders the scene depth onto a separate depth buffer from the light's view
	if (shadowIndex > 0)
	{
		if (Tweakables::g_ShowSDSM)
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
				for (auto& pShadowMap : m_ShadowMaps)
				{
					context.InsertResourceBarrier(pShadowMap.get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
				}

				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				context.SetGraphicsRootSignature(m_pShadowRS.get());

				struct ViewData
				{
					Matrix ViewProjection;
				}viewData;

				for (int i = 0; i < shadowIndex; ++i)
				{
					GPU_PROFILE_SCOPE("Light View", &context);

					GraphicsTexture* pShadowMap = m_ShadowMaps[i].get();
					context.BeginRenderPass(RenderPassInfo(pShadowMap, RenderPassAccess::Clear_Store));

					viewData.ViewProjection = shadowData.LightViewProjections[i];
					context.SetDynamicConstantBufferView(1, &viewData, sizeof(ViewData));

					struct PerObjectData
					{
						Matrix World;
						MaterialData Material;
					} objectData;

					context.SetDynamicDescriptors(2, 0, m_SceneData.MaterialTextures.data(), (int)m_SceneData.MaterialTextures.size());

					auto DrawBatches = [&](CommandContext& contet, const std::vector<Batch>& batches)
						{
							for (const Batch& b : batches)
							{
								objectData.World = b.WorldMatrix;
								objectData.Material = b.Material;
								context.SetDynamicConstantBufferView(0, &objectData, sizeof(PerObjectData));

								b.pMesh->Draw(&context);
							}
						};

					{
						GPU_PROFILE_SCOPE("Opaque", &context);
						context.SetPipelineState(m_pShadowPSO.get());
						DrawBatches(context, m_SceneData.OpaqueBatches);
					}

					{
						GPU_PROFILE_SCOPE("Transparent", &context);
						context.SetPipelineState(m_pShadowAlphaPSO.get());
						DrawBatches(context, m_SceneData.TransparentBatches);
					}
					context.EndRenderPass();
				}
			});
	}

	if (m_RenderPath == RenderPath::Tiled)
	{
		m_pTiledForward->Execute(graph, m_SceneData);
	}
	else if (m_RenderPath == RenderPath::Clustered)
	{
		m_pClusteredForward->Execute(graph, m_SceneData);
	}

	m_pGpuParticles->Render(graph, GetCurrentRenderTarget(), GetDepthStencil(), *m_pCamera);

	RGPassBuilder sky = graph.AddPass("Sky");
	sceneData.DepthStencil = sky.Read(sceneData.DepthStencil);
	sky.Bind([=](CommandContext& renderContext, const RGPassResource& inputResources)
		{
			GraphicsTexture* pDepthStencil = inputResources.GetTexture(sceneData.DepthStencil);

			renderContext.InsertResourceBarrier(pDepthStencil, D3D12_RESOURCE_STATE_DEPTH_READ);
			renderContext.InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET);

			RenderPassInfo info = RenderPassInfo(GetCurrentRenderTarget(), RenderPassAccess::Load_Store, pDepthStencil, RenderPassAccess::Load_DontCare);

			renderContext.BeginRenderPass(info);
			renderContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

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

	RGPassBuilder resolve = graph.AddPass("Resolve");
	resolve.Bind([=](CommandContext& context, const RGPassResource& resources)
		{
			if (m_SampleCount > 1)
			{
				context.InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
				GraphicsTexture* pTarget = Tweakables::g_TAA ? m_pTAASource.get() : m_pHDRRenderTarget.get();
				context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_RESOLVE_DEST);
				context.ResolveResource(GetCurrentRenderTarget(), 0, pTarget, 0, RENDER_TARGET_FORMAT);
			}

			if (!Tweakables::g_TAA)
			{
				context.CopyTexture(m_pHDRRenderTarget.get(), m_pPreviousColor.get());
			}
			else
			{
				context.CopyTexture(m_pHDRRenderTarget.get(), m_pTAASource.get());
			}
		});

	if (Tweakables::g_TAA)
	{
		// temporal resolve
		RGPassBuilder temporalResolve = graph.AddPass("Temporal Resolve");
		temporalResolve.Bind([=](CommandContext& context, const RGPassResource& resources)
			{
				context.InsertResourceBarrier(m_pTAASource.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				context.InsertResourceBarrier(m_pHDRRenderTarget.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(m_pVelocity.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(m_pPreviousColor.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

				context.SetComputeRootSignature(m_pTemporalResolveRS.get());
				context.SetPipelineState(Tweakables::g_TestTAA ? m_pTemporalResolveTestPSO.get() : m_pTemporalResolvePSO.get());

				struct TemporalParameters
				{
					Vector2 InvScreenDimensions;
					Vector2 Jitter;
				} parameters;

				parameters.InvScreenDimensions = Vector2(1.0f / m_WindowWidth, 1.0f / m_WindowHeight);
				parameters.Jitter.x = m_pCamera->GetPrevJitter().x - m_pCamera->GetJitter().x;
				parameters.Jitter.y = -(m_pCamera->GetPrevJitter().y - m_pCamera->GetJitter().y);
				context.SetComputeDynamicConstantBufferView(0, &parameters, sizeof(TemporalParameters));

				context.SetDynamicDescriptor(1, 0, m_pHDRRenderTarget->GetUAV());
				context.SetDynamicDescriptor(2, 0, m_pVelocity->GetSRV());
				context.SetDynamicDescriptor(2, 1, m_pPreviousColor->GetSRV());
				context.SetDynamicDescriptor(2, 2, m_pTAASource->GetSRV());
				context.SetDynamicDescriptor(2, 3, GetResolveDepthStencil()->GetSRV());

				int dispatchGroupX = Math::DivideAndRoundUp(m_WindowWidth, 8);
				int dispatchGroupY = Math::DivideAndRoundUp(m_WindowHeight, 8);
				context.Dispatch(dispatchGroupX, dispatchGroupY);

				context.CopyTexture(m_pHDRRenderTarget.get(), m_pPreviousColor.get());
			});
	}

	m_pClouds->Render(graph, m_pHDRRenderTarget.get(), GetResolveDepthStencil(), m_pCamera.get(), m_Lights[0]);

	// tonemap
	{
		bool downscaleTonemap = true;
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

					context.Dispatch(Math::DivideAndRoundUp(pTonemapInput->GetWidth(), 8), Math::DivideAndRoundUp(pTonemapInput->GetHeight(), 8));
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
				Parameters.MinLogLuminance = Tweakables::g_MinLogLuminance;
				Parameters.OneOverLogLuminanceRange = 1.0f / (Tweakables::g_MaxLogLuminance - Tweakables::g_MinLogLuminance);

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
				AverageParameters.MinLogLuminance = Tweakables::g_MinLogLuminance;
				AverageParameters.LogLuminanceRange = Tweakables::g_MaxLogLuminance - Tweakables::g_MinLogLuminance;
				AverageParameters.TimeDelta = Time::DeltaTime();
				AverageParameters.Tau = Tweakables::g_Tau;

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
				constBuffer.WhitePoint = Tweakables::g_WhitePoint;
				constBuffer.ToneMapper = Tweakables::g_ToneMapper;

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

		if (Tweakables::g_EnableUI && Tweakables::g_DrawHistogram)
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
						float MinLogLuminance{ 0 };
						float InverseLogLuminanceRange{ 0 };
					} Parameters;

					Parameters.MinLogLuminance = Tweakables::g_MinLogLuminance;
					Parameters.InverseLogLuminanceRange = 1.0f / (Tweakables::g_MaxLogLuminance - Tweakables::g_MinLogLuminance);

					context.SetComputeDynamicConstantBufferView(0, &Parameters, sizeof(AverageParameters));
					context.SetDynamicDescriptor(1, 0, m_pTonemapTarget->GetUAV());
					context.SetDynamicDescriptor(2, 0, m_pLuminanceHistogram->GetSRV());
					context.SetDynamicDescriptor(2, 1, m_pAverageLuminance->GetSRV());

					context.Dispatch(1, m_pLuminanceHistogram->GetDesc().ElementCount);
				});
		}
	}

	if (Tweakables::g_VisualizeLightDensity)
	{
		if (m_RenderPath == RenderPath::Tiled)
		{
			m_pTiledForward->VisualizeLightDensity(graph, *m_pCamera, m_pTonemapTarget.get(), GetResolveDepthStencil());
		}
		else if (m_RenderPath == RenderPath::Clustered)
		{
			m_pClusteredForward->VisualizeLightDensity(graph, *m_pCamera, m_pTonemapTarget.get(), GetResolveDepthStencil());
		}

		// render color legend
		ImGui::SetNextWindowSize(ImVec2(60, 255));
		ImGui::SetNextWindowPos(ImVec2((float)m_WindowWidth - 65, (float)m_WindowHeight - 280));
		ImGui::Begin("Visualize Light Density", 0, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);
		ImGui::SetWindowFontScale(1.2f);
		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
		static uint32_t DEBUG_COLORS[] = {
			IM_COL32(0,4,141, 255),
			IM_COL32(5,10,255, 255),
			IM_COL32(0,164,255, 255),
			IM_COL32(0,255,189, 255),
			IM_COL32(0,255,41, 255),
			IM_COL32(117,254,1, 255),
			IM_COL32(255,239,0, 255),
			IM_COL32(255,86,0, 255),
			IM_COL32(204,3,0, 255),
			IM_COL32(65,0,1, 255),
		};

		for (size_t i = 0; i < std::size(DEBUG_COLORS); ++i)
		{
			char number[16];
			sprintf_s(number, "%d", (int)i);
			ImGui::PushStyleColor(ImGuiCol_Button, DEBUG_COLORS[i]);
			ImGui::Button(number, ImVec2(40, 20));
			ImGui::PopStyleColor();
		}
		ImGui::PopStyleColor();
		ImGui::End();
	}

	// UI
	//  - ImGui render, pretty straight forward
	if (Tweakables::g_EnableUI)
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
	if (Tweakables::g_DumpRenderGraph)
	{
		graph.DumpGraphMermaid("graph.html");
		Tweakables::g_DumpRenderGraph = false;
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

	//m_FenceValues[m_CurrentBackBufferIndex] = fenceValue;
	m_pSwapchain->Present(1, 0);
	m_CurrentBackBufferIndex = m_pSwapchain->GetCurrentBackBufferIndex();
	//WaitForFence(m_FenceValues[m_CurrentBackBufferIndex]);
	++m_Frame;
}

void Graphics::InitD3D()
{
	E_LOG(Info, "Graphics::InitD3D");
	UINT dxgiFactoryFlags = 0;

	bool debugD3D = CommandLine::GetBool("d3ddebug") || D3D_VALIDATION;
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

	if (CommandLine::GetBool("dred"))
	{
		ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> pDredSettings;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pDredSettings))))
		{
			// turn on auto-breadcrumbs and page fault reporting
			pDredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			pDredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			E_LOG(Info, "DRED Enabled");
		}
	}

	// factory
	VERIFY_HR(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_pFactory)));

	bool allowTearing = false;
	HRESULT hr = m_pFactory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));
	allowTearing &= SUCCEEDED(hr);

	// look for an adapter
	ComPtr<IDXGIAdapter4> pAdapter;
	uint32_t adapter = 0;
	while (m_pFactory->EnumAdapterByGpuPreference(adapter++, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(pAdapter.ReleaseAndGetAddressOf())) != DXGI_ERROR_NOT_FOUND)
	{
		DXGI_ADAPTER_DESC3 desc;
		pAdapter->GetDesc3(&desc);

		if (!(desc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE))
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
	D3D::SetObjectName(m_pDevice.Get(), "Main Device");

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
				E_LOG(Warning, "D3D Validation Break on Serverity Enabled");
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
			m_SamplerFeedbackSupport = caps7.SamplerFeedbackTier;
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
	m_pResolveDepthStencil = std::make_unique<GraphicsTexture>(this, "Resolved Depth Stencil");

	if (m_SampleCount > 1)
	{
		m_pMultiSampleRenderTarget = std::make_unique<GraphicsTexture>(this, "MSAA Render Target");
	}

	m_pDynamicAllocationManager = std::make_unique<DynamicAllocationManager>(this);

	m_pImGuiRenderer = std::make_unique<ImGuiRenderer>(this);
	m_pImGuiRenderer->AddUpdateCallback(ImGuiCallbackDelegate::CreateRaw(this, &Graphics::UpdateImGui));

	m_pHDRRenderTarget = std::make_unique<GraphicsTexture>(this, "HDR Render Target");
	m_pPreviousColor = std::make_unique<GraphicsTexture>(this, "Previous Color");
	m_pTonemapTarget = std::make_unique<GraphicsTexture>(this, "Tonemap Target");
	m_pDownscaledColor = std::make_unique<GraphicsTexture>(this, "Downscaled HDR Target");
	m_pAmbientOcclusion = std::make_unique<GraphicsTexture>(this, "SSAO Target");
	m_pVelocity = std::make_unique<GraphicsTexture>(this, "Velocity");
	m_pTAASource = std::make_unique<GraphicsTexture>(this, "TAA Target");

	m_pClusteredForward = std::make_unique<ClusteredForward>(this);
	m_pTiledForward = std::make_unique<TiledForward>(this);

	m_pRTAO = std::make_unique<RTAO>(this);
	m_pSSAO = std::make_unique<SSAO>(this);
	m_pRTReflections = std::make_unique<RTReflections>(this);
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
	swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapchainDesc.Flags = 0;
	swapchainDesc.SampleDesc.Count = 1;  // must set for msaa >= 1, not 0
	swapchainDesc.SampleDesc.Quality = 0;
	swapchainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	swapchainDesc.Stereo = false;
	swapchainDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

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
	DXGI_SWAP_CHAIN_DESC1 desc{};
	m_pSwapchain->GetDesc1(&desc);
	VERIFY_HR_EX(m_pSwapchain->ResizeBuffers(
		FRAME_COUNT,
		m_WindowWidth,
		m_WindowHeight,
		desc.Format,
		desc.Flags), GetDevice());

	m_CurrentBackBufferIndex = 0;

	// recreate the render target views
	for (int i = 0; i < FRAME_COUNT; i++)
	{
		ID3D12Resource* pResource = nullptr;
		VERIFY_HR_EX(m_pSwapchain->GetBuffer(i, IID_PPV_ARGS(&pResource)), GetDevice());
		m_Backbuffers[i]->CreateForSwapChain(pResource);
	}

	m_pDepthStencil->Create(TextureDesc::CreateDepth(m_WindowWidth, m_WindowHeight, DEPTH_STENCIL_FORMAT, TextureFlag::DepthStencil | TextureFlag::ShaderResource, m_SampleCount, ClearBinding(0.0f, 0)));
	m_pResolveDepthStencil->Create(TextureDesc::Create2D(m_WindowWidth, m_WindowHeight, DXGI_FORMAT_R32_FLOAT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess));
	if (m_SampleCount > 1)
	{
		m_pMultiSampleRenderTarget->Create(TextureDesc::CreateRenderTarget(width, height, RENDER_TARGET_FORMAT, TextureFlag::RenderTarget, m_SampleCount, ClearBinding(Color(0, 0, 0, 0))));
	}

	m_pHDRRenderTarget->Create(TextureDesc::CreateRenderTarget(width, height, RENDER_TARGET_FORMAT, TextureFlag::ShaderResource | TextureFlag::RenderTarget | TextureFlag::UnorderedAccess));
	m_pPreviousColor->Create(TextureDesc::Create2D(width, height, RENDER_TARGET_FORMAT, TextureFlag::ShaderResource));
	m_pTonemapTarget->Create(TextureDesc::CreateRenderTarget(width, height, SWAPCHAIN_FORMAT, TextureFlag::ShaderResource | TextureFlag::RenderTarget | TextureFlag::UnorderedAccess));
	m_pDownscaledColor->Create(TextureDesc::Create2D(Math::DivideAndRoundUp(width, 4), Math::DivideAndRoundUp(height, 4), RENDER_TARGET_FORMAT, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource));
	m_pVelocity->Create(TextureDesc::Create2D(width, height, DXGI_FORMAT_R16G16_FLOAT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess));
	m_pTAASource->Create(TextureDesc::CreateRenderTarget(width, height, RENDER_TARGET_FORMAT, TextureFlag::ShaderResource | TextureFlag::RenderTarget | TextureFlag::UnorderedAccess));
	m_pAmbientOcclusion->Create(TextureDesc::Create2D(Math::DivideAndRoundUp(width, 2), Math::DivideAndRoundUp(height, 2), DXGI_FORMAT_R8_UNORM, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess));

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

void Graphics::GenerateAccelerationStructure(Mesh* pMesh, CommandContext& context)
{
	if (!SupportsRaytracing())
	{
		return;
	}

	ID3D12GraphicsCommandList4* pCmd = context.GetRaytracingCommandList();

	// bottom level acceleration structure
	{
		std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries;
		for (size_t i = 0; i < pMesh->GetMeshCount(); i++)
		{
			const SubMesh* pSubMesh = pMesh->GetMesh((int)i);
			if (pMesh->GetMaterial(pSubMesh->GetMaterialId()).IsTransparent)
			{
				continue; // skip transparent meshes
			}

			D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc{};
			geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
			geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
			geometryDesc.Triangles.IndexBuffer = pSubMesh->GetIndexBuffer().Location;
			geometryDesc.Triangles.IndexCount = pSubMesh->GetIndexBuffer().Elements;
			geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
			geometryDesc.Triangles.Transform3x4 = 0;
			geometryDesc.Triangles.VertexBuffer.StartAddress = pSubMesh->GetVertexBuffer().Location;
			geometryDesc.Triangles.VertexBuffer.StrideInBytes = pSubMesh->GetVertexBuffer().Stride;
			geometryDesc.Triangles.VertexCount = pSubMesh->GetVertexBuffer().Elements;
			geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
			geometries.push_back(geometryDesc);
		}

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS prebuildInfo = {
			.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
			.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
					D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION,
			.NumDescs = (uint32_t)geometries.size(),
			.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
			.pGeometryDescs = geometries.data(),
		};

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
		GetRaytracingDevice()->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfo, &info);

		m_pBLASScratch = std::make_unique<Buffer>(this, "BLAS Scratch Buffer");
		m_pBLASScratch->Create(BufferDesc::CreateByteAddress(Math::AlignUp<uint64_t>(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::None));
		m_pBLAS = std::make_unique<Buffer>(this, "BLAS");
		m_pBLAS->Create(BufferDesc::CreateAccelerationStructure(Math::AlignUp<uint64_t>(info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)));

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {
			.DestAccelerationStructureData = m_pBLAS->GetGpuHandle(),
			.Inputs = prebuildInfo,
			.SourceAccelerationStructureData = 0,
			.ScratchAccelerationStructureData = m_pBLASScratch->GetGpuHandle(),
		};

		pCmd->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
		context.InsertUavBarrier(m_pBLAS.get());
		context.FlushResourceBarriers();
	}

	// top level acceleration structure
	{
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS prebuildInfo = {
			.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
			.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
					D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION,
			.NumDescs = 1,
			.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
		};

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
		GetRaytracingDevice()->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfo, &info);

		m_pTLASScratch = std::make_unique<Buffer>(this, "TLAS Scratch");
		m_pTLASScratch->Create(BufferDesc::CreateByteAddress(Math::AlignUp<uint64_t>(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::None));
		m_pTLAS = std::make_unique<Buffer>(this, "TLAS");
		m_pTLAS->Create(BufferDesc::CreateAccelerationStructure(Math::AlignUp<uint64_t>(info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)));

		DynamicAllocation allocation = context.AllocateTransientMemory(sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
		D3D12_RAYTRACING_INSTANCE_DESC* pInstanceDesc = static_cast<D3D12_RAYTRACING_INSTANCE_DESC*>(allocation.pMappedMemory);
		pInstanceDesc->AccelerationStructure = m_pBLAS->GetGpuHandle();
		pInstanceDesc->Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		pInstanceDesc->InstanceContributionToHitGroupIndex = 0;
		pInstanceDesc->InstanceID = 0;
		pInstanceDesc->InstanceMask = 0xFF;

		// the layout of Transform is a transpose of how affine matrices are typically stored in memory. 
		// Instead of four 3-vectors. Transform is laid out as three 4-vectors.
		auto ApplyTransform = [](const Matrix& m, D3D12_RAYTRACING_INSTANCE_DESC& desc)
			{
				desc.Transform[0][0] = m.m[0][0]; desc.Transform[0][1] = m.m[1][0]; desc.Transform[0][2] = m.m[2][0]; desc.Transform[0][3] = m.m[3][0];
				desc.Transform[1][0] = m.m[0][1]; desc.Transform[1][1] = m.m[1][1]; desc.Transform[1][2] = m.m[2][1]; desc.Transform[1][3] = m.m[3][1];
				desc.Transform[2][0] = m.m[0][2]; desc.Transform[2][1] = m.m[1][2]; desc.Transform[2][2] = m.m[2][2]; desc.Transform[2][3] = m.m[3][2];
			};
		ApplyTransform(Matrix::Identity, *pInstanceDesc);

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {
			.DestAccelerationStructureData = m_pTLAS->GetGpuHandle(),
			.Inputs = {
				.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
				.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE,
				.NumDescs = 1,
				.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
				.InstanceDescs = allocation.GpuHandle,
			},
			.SourceAccelerationStructureData = 0,
			.ScratchAccelerationStructureData = m_pTLASScratch->GetGpuHandle(),
		};

		pCmd->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
		context.InsertUavBarrier(m_pTLAS.get());
		context.FlushResourceBarriers();
	}
}

void Graphics::InitializeAssets(CommandContext& context)
{
	m_pMesh = std::make_unique<Mesh>();
	m_pMesh->Load("Resources/sponza/sponza.dae", this, &context);

	std::map<GraphicsTexture*, int> textureToIndex;
	int textureIndex = 0;
	for (int i = 0; i < m_pMesh->GetMeshCount(); i++)
	{
		const Material& material = m_pMesh->GetMaterial(m_pMesh->GetMesh(i)->GetMaterialId());

		auto it = textureToIndex.find(material.pDiffuseTexture);
		if (it == textureToIndex.end())
		{
			textureToIndex[material.pDiffuseTexture] = textureIndex++;
		}
		it = textureToIndex.find(material.pNormalTexture);
		if (it == textureToIndex.end())
		{
			textureToIndex[material.pNormalTexture] = textureIndex++;
		}
		it = textureToIndex.find(material.pRoughnessTexture);
		if (it == textureToIndex.end())
		{
			textureToIndex[material.pRoughnessTexture] = textureIndex++;
		}
		it = textureToIndex.find(material.pMetallicTexture);
		if (it == textureToIndex.end())
		{
			textureToIndex[material.pMetallicTexture] = textureIndex++;
		}
	}

	m_SceneData.MaterialTextures.resize(textureToIndex.size());
	for (auto& pair : textureToIndex)
	{
		m_SceneData.MaterialTextures[pair.second] = pair.first->GetSRV();
	}

	for (int i = 0; i < m_pMesh->GetMeshCount(); i++)
	{
		const Material& material = m_pMesh->GetMaterial(m_pMesh->GetMesh(i)->GetMaterialId());
		Batch b;
		b.Bounds = m_pMesh->GetMesh(i)->GetBounds();
		b.pMesh = m_pMesh->GetMesh(i);

		b.Material.Diffuse = textureToIndex[material.pDiffuseTexture];
		b.Material.Normal = textureToIndex[material.pNormalTexture];
		b.Material.Roughness = textureToIndex[material.pRoughnessTexture];
		b.Material.Metallic = textureToIndex[material.pMetallicTexture];

		if (material.IsTransparent)
		{
			m_SceneData.TransparentBatches.push_back(b);
		}
		else
		{
			m_SceneData.OpaqueBatches.push_back(b);
		}
	}

	GenerateAccelerationStructure(m_pMesh.get(), context);

	{
		int lightCount = 5;
		m_Lights.resize(lightCount);

		Vector3 position(-150, 160, -10);
		Vector3 direction;
		position.Normalize(direction);

		m_Lights[0] = Light::Directional(position, -direction, 10.0f);
		m_Lights[0].CastShadows = true;
		m_Lights[0].VolumetricLighting = true;

		m_Lights[1] = Light::Spot(Vector3(62, 10, -18), 200, Vector3(0, 1, 0), 90, 70, 1000, Color(1, 0.7f, 0.3f, 1.0f));
		m_Lights[1].CastShadows = true;
		m_Lights[1].VolumetricLighting = true;

		m_Lights[2] = Light::Spot(Vector3(-48, 10, 18), 200, Vector3(0, 1, 0), 90, 70, 1000, Color(1, 0.7f, 0.3f, 1.0f));
		m_Lights[2].CastShadows = true;
		m_Lights[2].VolumetricLighting = true;

		m_Lights[3] = Light::Spot(Vector3(-48, 10, -18), 200, Vector3(0, 1, 0), 90, 70, 1000, Color(1, 0.7f, 0.3f, 1.0f));
		m_Lights[3].CastShadows = true;
		m_Lights[3].VolumetricLighting = true;

		m_Lights[4] = Light::Spot(Vector3(62, 10, 18), 200, Vector3(0, 1, 0), 90, 70, 1000, Color(1, 0.7f, 0.3f, 1.0f));
		m_Lights[4].CastShadows = true;
		m_Lights[4].VolumetricLighting = true;

		m_pLightBuffer = std::make_unique<Buffer>(this, "Lights");
		m_pLightBuffer->Create(BufferDesc::CreateStructured((uint32_t)m_Lights.size(), sizeof(Light::RenderData), BufferFlag::ShaderResource));
	}
}

void Graphics::InitializePipelines()
{
	// input layout
	CD3DX12_INPUT_ELEMENT_DESC depthOnlyInputElements[] = {
			CD3DX12_INPUT_ELEMENT_DESC{ "POSITION", DXGI_FORMAT_R32G32B32_FLOAT },
			CD3DX12_INPUT_ELEMENT_DESC{ "TEXCOORD", DXGI_FORMAT_R32G32B32_FLOAT },
	};

	// shadow mapping
	// vertex shader-only pass that writes to the depth buffer using the light matrix
	{
		Shader vertexShader("DepthOnly.hlsl", ShaderType::Vertex, "VSMain");
		Shader alphaClipPixelShader("DepthOnly.hlsl", ShaderType::Pixel, "PSMain");

		// root signature
		m_pShadowRS = std::make_unique<RootSignature>(this);
		m_pShadowRS->FinalizeFromShader("Shadow Mapping RS", vertexShader);

		// pipeline state
		m_pShadowPSO = std::make_unique<PipelineState>(this);
		m_pShadowPSO->SetInputLayout(depthOnlyInputElements, sizeof(depthOnlyInputElements) / sizeof(depthOnlyInputElements[0]));
		m_pShadowPSO->SetRootSignature(m_pShadowRS->GetRootSignature());
		m_pShadowPSO->SetVertexShader(vertexShader);
		m_pShadowPSO->SetRenderTargetFormats(nullptr, 0, DEPTH_STENCIL_SHADOW_FORMAT, 1);
		m_pShadowPSO->SetCullMode(D3D12_CULL_MODE_NONE);
		m_pShadowPSO->SetDepthBias(-1, -5.f, -4.f);
		m_pShadowPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		m_pShadowPSO->Finalize("Shadow Mapping (Opaque)");


		m_pShadowAlphaPSO = std::make_unique<PipelineState>(*m_pShadowPSO);
		m_pShadowAlphaPSO->SetPixelShader(alphaClipPixelShader);
		m_pShadowAlphaPSO->Finalize("Shadow Mapping (Alpha)");
	}

	// depth prepass
	// simple vertex shader to fill the depth buffer to optimize later passes
	{
		Shader vertexShader("DepthOnly.hlsl", ShaderType::Vertex, "VSMain");

		// root signature
		m_pDepthPrepassRS = std::make_unique<RootSignature>(this);
		m_pDepthPrepassRS->FinalizeFromShader("Depth Prepass RS", vertexShader);

		// pipeline state
		m_pDepthPrepassPSO = std::make_unique<PipelineState>(this);
		m_pDepthPrepassPSO->SetInputLayout(depthOnlyInputElements, std::size(depthOnlyInputElements));
		m_pDepthPrepassPSO->SetRootSignature(m_pDepthPrepassRS->GetRootSignature());
		m_pDepthPrepassPSO->SetVertexShader(vertexShader);
		m_pDepthPrepassPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		m_pDepthPrepassPSO->SetRenderTargetFormats(nullptr, 0, DEPTH_STENCIL_FORMAT, m_SampleCount);
		m_pDepthPrepassPSO->Finalize("Depth Prepass");
	}

	// luminance histogram
	{
		Shader computeShader("Tonemap/LuminanceHistogram.hlsl", ShaderType::Compute, "CSMain");

		// root signature
		m_pLuminanceHistogramRS = std::make_unique<RootSignature>(this);
		m_pLuminanceHistogramRS->FinalizeFromShader("Luminance Histogram RS", computeShader);

		// pipeline state
		m_pLuminanceHistogramPSO = std::make_unique<PipelineState>(this);
		m_pLuminanceHistogramPSO->SetComputeShader(computeShader);
		m_pLuminanceHistogramPSO->SetRootSignature(m_pLuminanceHistogramRS->GetRootSignature());
		m_pLuminanceHistogramPSO->Finalize("Luminance Histogram PSO");

		m_pLuminanceHistogram = std::make_unique<Buffer>(this, "Luminance Histogram");
		m_pLuminanceHistogram->Create(BufferDesc::CreateByteAddress(sizeof(uint32_t) * 256));
		m_pAverageLuminance = std::make_unique<Buffer>(this, "Average Luminance");
		m_pAverageLuminance->Create(BufferDesc::CreateStructured(3, sizeof(float), BufferFlag::UnorderedAccess | BufferFlag::ShaderResource));
	}

	// draw histogram
	{
		Shader computeShader("Tonemap/DrawLuminanceHistogram.hlsl", ShaderType::Compute, "DrawLuminanceHistogram");
		m_pDrawHistogramRS = std::make_unique<RootSignature>(this);
		m_pDrawHistogramRS->FinalizeFromShader("Draw Histogram RS", computeShader);

		m_pDrawHistogramPSO = std::make_unique<PipelineState>(this);
		m_pDrawHistogramPSO->SetComputeShader(computeShader);
		m_pDrawHistogramPSO->SetRootSignature(m_pDrawHistogramRS->GetRootSignature());
		m_pDrawHistogramPSO->Finalize("Draw Histogram PSO");
	}

	// average luminance
	{
		Shader computeShader("Tonemap/AverageLuminance.hlsl", ShaderType::Compute, "CSMain");

		// root signature
		m_pAverageLuminanceRS = std::make_unique<RootSignature>(this);
		m_pAverageLuminanceRS->FinalizeFromShader("Average Luminance RS", computeShader);

		// pipeline state
		m_pAverageLuminancePSO = std::make_unique<PipelineState>(this);
		m_pAverageLuminancePSO->SetComputeShader(computeShader);
		m_pAverageLuminancePSO->SetRootSignature(m_pAverageLuminanceRS->GetRootSignature());
		m_pAverageLuminancePSO->Finalize("Average Luminance PSO");
	}

	// camera motion
	{
		Shader computeShader("CameraMotionVectors.hlsl", ShaderType::Compute, "CSMain");

		m_pCameraMotionRS = std::make_unique<RootSignature>(this);
		m_pCameraMotionRS->FinalizeFromShader("Camera Motion RS", computeShader);

		m_pCameraMotionPSO = std::make_unique<PipelineState>(this);
		m_pCameraMotionPSO->SetComputeShader(computeShader);
		m_pCameraMotionPSO->SetRootSignature(m_pCameraMotionRS->GetRootSignature());
		m_pCameraMotionPSO->Finalize("Camera Motion PSO");
	}

	// tonemapping
	{
		Shader computeShader("Tonemap/Tonemapping.hlsl", ShaderType::Compute, "CSMain");

		// rootSignature
		m_pToneMapRS = std::make_unique<RootSignature>(this);
		m_pToneMapRS->FinalizeFromShader("Tonemapping RS", computeShader);

		// pipeline state
		m_pToneMapPSO = std::make_unique<PipelineState>(this);
		m_pToneMapPSO->SetRootSignature(m_pToneMapRS->GetRootSignature());
		m_pToneMapPSO->SetComputeShader(computeShader);
		m_pToneMapPSO->Finalize("Tonemapping PSO");
	}

	// depth resolve
	// resolves a multisampled buffer to a normal depth buffer
	// only required when the sample count > 1
	{
		Shader computeShader("ResolveDepth.hlsl", ShaderType::Compute, "CSMain");

		m_pResolveDepthRS = std::make_unique<RootSignature>(this);
		m_pResolveDepthRS->FinalizeFromShader("Resolve Depth RS", computeShader);

		m_pResolveDepthPSO = std::make_unique<PipelineState>(this);
		m_pResolveDepthPSO->SetComputeShader(computeShader);
		m_pResolveDepthPSO->SetRootSignature(m_pResolveDepthRS->GetRootSignature());
		m_pResolveDepthPSO->Finalize("Resolve Depth Pipeline");
	}

	// depth reduce
	{
		Shader prepareReduceShader("ReduceDepth.hlsl", ShaderType::Compute, "PrepareReduceDepth");
		Shader prepareReduceShaderMSAA("ReduceDepth.hlsl", ShaderType::Compute, "PrepareReduceDepth", { "WITH_MSAA" });
		Shader reduceShader("ReduceDepth.hlsl", ShaderType::Compute, "ReduceDepth");

		m_pReduceDepthRS = std::make_unique<RootSignature>(this);
		m_pReduceDepthRS->FinalizeFromShader("Reduce Depth RS", reduceShader);

		m_pPrepareReduceDepthPSO = std::make_unique<PipelineState>(this);
		m_pPrepareReduceDepthPSO->SetComputeShader(prepareReduceShader);
		m_pPrepareReduceDepthPSO->SetRootSignature(m_pReduceDepthRS->GetRootSignature());
		m_pPrepareReduceDepthPSO->Finalize("Prepare Reduce Depth PSO");

		m_pPrepareReduceDepthMsaaPSO = std::make_unique<PipelineState>(*m_pPrepareReduceDepthPSO);
		m_pPrepareReduceDepthMsaaPSO->SetComputeShader(prepareReduceShaderMSAA);
		m_pPrepareReduceDepthMsaaPSO->Finalize("Prepare Reduce Depth MSAA PSO");

		m_pReduceDepthPSO = std::make_unique<PipelineState>(*m_pPrepareReduceDepthPSO);
		m_pReduceDepthPSO->SetComputeShader(reduceShader);
		m_pReduceDepthPSO->Finalize("Reduce Depth PSO");
	}

	//TAA
	{
		{
			Shader computeShader("TemporalResolve.hlsl", ShaderType::Compute, "CSMain");
			
			m_pTemporalResolveRS = std::make_unique<RootSignature>(this);
			m_pTemporalResolveRS->FinalizeFromShader("Temporal Resolve RS", computeShader);
						
			m_pTemporalResolvePSO = std::make_unique<PipelineState>(this);
			m_pTemporalResolvePSO->SetComputeShader(computeShader);
			m_pTemporalResolvePSO->SetRootSignature(m_pTemporalResolveRS->GetRootSignature());
			m_pTemporalResolvePSO->Finalize("Temporal Resolve PSO ");
		}

		{
			Shader computeShader("TemporalResolve.hlsl", ShaderType::Compute, "CSMain", { "TAA_TEST" });
			
			m_pTemporalResolveTestPSO = std::make_unique<PipelineState>(*m_pTemporalResolvePSO);
			m_pTemporalResolveTestPSO->SetComputeShader(computeShader);
			m_pTemporalResolveTestPSO->Finalize("Temporal Resolve Test PSO ");
		}
	}

	// mip generation
	{
		Shader computeShader("GenerateMips.hlsl", ShaderType::Compute, "CSMain");

		m_pGenerateMipsRS = std::make_unique<RootSignature>(this);
		m_pGenerateMipsRS->FinalizeFromShader("Generate Mips RS", computeShader);

		m_pGenerateMipsPSO = std::make_unique<PipelineState>(this);
		m_pGenerateMipsPSO->SetComputeShader(computeShader);
		m_pGenerateMipsPSO->SetRootSignature(m_pGenerateMipsRS->GetRootSignature());
		m_pGenerateMipsPSO->Finalize("Generate Mips PSO");
	}

	// sky
	{
		Shader vertexShader("ProceduralSky.hlsl", ShaderType::Vertex, "VSMain");
		Shader pixelShader("ProceduralSky.hlsl", ShaderType::Pixel, "PSMain");

		// root signature
		m_pSkyboxRS = std::make_unique<RootSignature>(this);
		m_pSkyboxRS->FinalizeFromShader("Skybox RS", vertexShader);

		// pipeline state
		m_pSkyboxPSO = std::make_unique<PipelineState>(this);
		m_pSkyboxPSO->SetInputLayout(nullptr, 0);
		m_pSkyboxPSO->SetRootSignature(m_pSkyboxRS->GetRootSignature());
		m_pSkyboxPSO->SetVertexShader(vertexShader);
		m_pSkyboxPSO->SetPixelShader(pixelShader);
		m_pSkyboxPSO->SetRenderTargetFormat(RENDER_TARGET_FORMAT, DEPTH_STENCIL_FORMAT, m_SampleCount);
		m_pSkyboxPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		m_pSkyboxPSO->Finalize("Skybox");
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

	ImGui::Text("Camera: [%f, %f, %f]", m_pCamera->GetPosition().x, m_pCamera->GetPosition().y, m_pCamera->GetPosition().z);

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

		ImGui::Separator();

		if (ImGui::Button("Dump RenderGraph"))
		{
			Tweakables::g_DumpRenderGraph = true;
		}
		if (ImGui::Button("Screenshot"))
		{
			Tweakables::g_Screenshot = true;
		}
		if (ImGui::Button("Pix Capture"))
		{
			m_CapturePix = true;
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

	// m_pVisualizeTexture = m_pClouds->GetNoiseTexture();
	m_pVisualizeTexture = m_pVelocity.get();
	if (m_pVisualizeTexture)
	{
		ImGui::Begin("Visualize Texture");

		static bool visibleChannels[4] = { true, true, true, true };
		static int mipLevel = 0;
		static int sliceIndex = 0;
		ImGui::Checkbox("R", &visibleChannels[0]);
		ImGui::SameLine();
		ImGui::Checkbox("G", &visibleChannels[1]);
		ImGui::SameLine();
		ImGui::Checkbox("B", &visibleChannels[2]);
		ImGui::SameLine();
		ImGui::Checkbox("A", &visibleChannels[3]);
		ImGui::SameLine();
		ImGui::Text("Resolution: %dx%d", m_pVisualizeTexture->GetWidth(), m_pVisualizeTexture->GetHeight());

		Vector2 image((float)m_pVisualizeTexture->GetWidth(), (float)m_pVisualizeTexture->GetHeight());
		Vector2 windowSize(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);
		float width = windowSize.x;
		float height = windowSize.x * image.y / image.x;
		if (image.x / windowSize.x < image.y / windowSize.y)
		{
			width = image.x / image.y * windowSize.y;
			height = windowSize.y;
		}

		ImGui::SameLine();
		height -= ImGui::GetFontSize();
		ImGui::Text("View Resolution: %dx%d", (int)width, (int)height);
		if (m_pVisualizeTexture->GetMipLevels() > 1)
		{
			ImGui::SliderInt("Mip Level", &mipLevel, 0, m_pVisualizeTexture->GetMipLevels() - 1);
		}
		if (m_pVisualizeTexture->GetDepth() > 1)
		{
			ImGui::SliderInt("Slice Index", &sliceIndex, 0, m_pVisualizeTexture->GetDepth() - 1);
		}

		mipLevel = Math::Clamp(mipLevel, 0, (int)m_pVisualizeTexture->GetMipLevels() - 1);
		sliceIndex = Math::Clamp(sliceIndex, 0, (int)m_pVisualizeTexture->GetDepth() - 1);

		ImGui::Image(ImTextureData(m_pVisualizeTexture, visibleChannels[0], visibleChannels[1], visibleChannels[2], visibleChannels[3], mipLevel, sliceIndex), ImVec2(width, height));
		ImGui::End();
	}

	if (Tweakables::g_VisualizeShadowCascades && m_ShadowMaps.size() >= 4)
	{
		float imageSize = 230;
		ImGui::SetNextWindowSize(ImVec2(imageSize, 1024));
		ImGui::SetNextWindowPos(ImVec2(m_WindowWidth - imageSize, 0));
		ImGui::Begin("Shadow Cascades", 0, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
		const Light& sunLight = m_Lights[0];
		for (int i = 0; i < 4; ++i)
		{
			ImGui::Image(ImTextureData(m_ShadowMaps[sunLight.ShadowIndex + i].get()), ImVec2(imageSize, imageSize));
		}
		ImGui::End();
	}

	ImGui::SetNextWindowPos(ImVec2(300, 20), 0, ImVec2(0, 0));
	ImGui::Begin("Parameters");

	ImGui::Text("Sky");
	ImGui::SliderFloat("Sun Orientation", &Tweakables::g_SunOrientation, -Math::PI, Math::PI);
	ImGui::SliderFloat("Sun Inclination", &Tweakables::g_SunInclination, 0.001f, 1);
	ImGui::SliderFloat("Sun Temperature", &Tweakables::g_SunTemperature, 1000, 15000);
	ImGui::SliderFloat("Sun Intensity", &Tweakables::g_SunIntensity, 0, 30);

	ImGui::Text("Shadows");
	ImGui::SliderInt("Shadow Cascades", &Tweakables::g_ShadowCascades, 1, 4);
	ImGui::Checkbox("SDSM", &Tweakables::g_ShowSDSM);
	ImGui::Checkbox("Stabilize Cascades", &Tweakables::g_StabilizeCascases);
	ImGui::SliderFloat("PSSM Factor", &Tweakables::g_PSSMFactor, 0, 1);
	ImGui::Checkbox("Visualize Cascades", &Tweakables::g_VisualizeShadowCascades);

	ImGui::Text("Expose/Tonemapping");
	ImGui::SliderFloat("Min Log Luminance", &Tweakables::g_MinLogLuminance, -100, 20);
	ImGui::SliderFloat("Max Log Luminance", &Tweakables::g_MaxLogLuminance, -50, 50);
	ImGui::Checkbox("Draw Exposure Histogram", &Tweakables::g_DrawHistogram);
	ImGui::SliderFloat("White Point", &Tweakables::g_WhitePoint, 0, 20);
	ImGui::Combo("Tonemapper", (int*)&Tweakables::g_ToneMapper, [](void* data, int index, const char** outText)
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
	ImGui::SliderFloat("Tau", &Tweakables::g_Tau, 0, 5);

	ImGui::Text("Misc");
	ImGui::Checkbox("Debug Render Light", &Tweakables::g_VisualizeLights);
	ImGui::Checkbox("Visualize Light Density", &Tweakables::g_VisualizeLightDensity);
	extern bool g_VisualizeClusters;
	ImGui::Checkbox("Visualize Clusters", &g_VisualizeClusters);
	ImGui::SliderInt("SSR Samples", &Tweakables::g_SsrSamples, 0, 32);

	if (m_RayTracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
	{
		ImGui::Checkbox("Raytraced AO", &Tweakables::g_RaytracedAO);
		ImGui::Checkbox("Raytraced Reflections", &Tweakables::g_RaytracedReflections);
	}

	ImGui::Checkbox("TAA", &Tweakables::g_TAA);
	ImGui::Checkbox("Test TAA", &Tweakables::g_TestTAA);

	ImGui::End();
}

CommandQueue* Graphics::GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const
{
	return m_CommandQueues.at(type).get();
}

CommandContext* Graphics::AllocateCommandContext(D3D12_COMMAND_LIST_TYPE type)
{
	int typeIndex = type;
	CommandContext* pCommandContext = nullptr;

	{
		std::scoped_lock lockGuard(m_ContextAllocationMutex);

		if (m_FreeCommandContexts[typeIndex].size() > 0)
		{
			pCommandContext = m_FreeCommandContexts[typeIndex].front();
			m_FreeCommandContexts[typeIndex].pop();
			pCommandContext->Reset();
		}
		else
		{
			ComPtr<ID3D12CommandList> pCommandList;
			ID3D12CommandAllocator* pAllocator = m_CommandQueues[type]->RequestAllocator();
			VERIFY_HR(m_pDevice->CreateCommandList(0, type, pAllocator, nullptr, IID_PPV_ARGS(pCommandList.GetAddressOf())));
			D3D::SetObjectName(pCommandList.Get(), "Pooled CommandList");
			m_CommandLists.push_back(std::move(pCommandList));
			m_CommandListPool[typeIndex].emplace_back(std::make_unique<CommandContext>(this, static_cast<ID3D12GraphicsCommandList*>(m_CommandLists.back().Get()), type, pAllocator));
			pCommandContext = m_CommandListPool[typeIndex].back().get();
		}
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

