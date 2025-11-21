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
#include "Core/Paths.h"
#include "StateObject.h"
#include "External/ImGuizmo/ImGuizmo.h"

#ifndef D3D_VALIDATION
#define D3D_VALIDATION 1
#endif

#ifndef GPU_VALIDATION
#define GPU_VALIDATION 0
#endif

namespace Tweakables
{
	// post processing
	float g_WhitePoint = 1;
	float g_MinLogLuminance = -10.0f;
	float g_MaxLogLuminance = 20.0f;
	float g_Tau = 2;
	bool g_DrawHistogram = false;
	uint32_t g_ToneMapper = 2;
	bool g_TAA = true;

	// shadows
	bool g_ShowSDSM = false;
	bool g_StabilizeCascases = true;
	bool g_VisualizeShadowCascades = false;
	int g_ShadowCascades = 4;
	float g_PSSMFactor = 1.0f;

	// misc lighting
	bool g_RaytracedAO = false;
	bool g_VisualizeLights = false;
	bool g_VisualizeLightDensity = false;

	// lighting
	float g_SunInclination = 0.2f;
	float g_SunOrientation = -3.055f;
	float g_SunTemperature = 5900.0f;
	float g_SunIntensity = 3.0f;

	// reflections
	bool g_RaytracedReflections = false;
	int g_SsrSamples = 8;
	
	// Misc
	bool g_DumpRenderGraph = false;
	bool g_Screenshot = false;
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
	m_pCamera->SetNearPlane(300.f);
	m_pCamera->SetFarPlane(1.f);

	InitD3D();
	InitializePipelines();

	CommandContext* pContext = AllocateCommandContext();
	InitializeAssets(*pContext);
	SetupScene(*pContext);
	UpdateTLAS(*pContext);
	pContext->Execute(true);

	m_pDynamicAllocationManager->CollectGrabage();
}

void EditTransform(const Camera& camera, Matrix& matrix)
{
	static ImGuizmo::OPERATION mCurrentGizmoOperation(ImGuizmo::ROTATE);
	static ImGuizmo::MODE mCurrentGizmoMode(ImGuizmo::WORLD);

	if (!Input::Instance().IsMouseDown(VK_LBUTTON))
	{
		if (Input::Instance().IsKeyPressed('W'))
			mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
		else if (Input::Instance().IsKeyPressed('E'))
			mCurrentGizmoOperation = ImGuizmo::ROTATE;
		else if (Input::Instance().IsKeyPressed('R'))
			mCurrentGizmoOperation = ImGuizmo::SCALE;
	}

	if (ImGui::RadioButton("Translate", mCurrentGizmoOperation == ImGuizmo::TRANSLATE))
	{
		mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
	}
	if (ImGui::RadioButton("Rotate", mCurrentGizmoOperation == ImGuizmo::ROTATE))
	{
		mCurrentGizmoOperation = ImGuizmo::ROTATE;
	}
	if (ImGui::RadioButton("Scale", mCurrentGizmoOperation == ImGuizmo::SCALE))
	{
		mCurrentGizmoOperation = ImGuizmo::SCALE;
	}

	float matrixTranslate[3], matrixRotation[3], matrixScale[3];
	ImGuizmo::DecomposeMatrixToComponents(&matrix.m[0][0], matrixTranslate, matrixRotation, matrixScale);
	ImGui::InputFloat3("Tr", matrixTranslate);
	ImGui::InputFloat3("Rt", matrixRotation);
	ImGui::InputFloat3("Sc", matrixScale);
	ImGuizmo::RecomposeMatrixFromComponents(matrixTranslate, matrixRotation, matrixScale, &matrix.m[0][0]);

	if (mCurrentGizmoMode != ImGuizmo::SCALE)
	{
		if (ImGui::RadioButton("Local", mCurrentGizmoMode == ImGuizmo::LOCAL))
		{
			mCurrentGizmoMode = ImGuizmo::LOCAL;
		}
		ImGui::SameLine();
		if (ImGui::RadioButton("World", mCurrentGizmoMode == ImGuizmo::WORLD))
		{
			mCurrentGizmoMode = ImGuizmo::WORLD;
		}

		if (Input::Instance().IsKeyPressed(VK_SPACE))
		{
			mCurrentGizmoMode = mCurrentGizmoMode == ImGuizmo::LOCAL ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
		}
	}

	static Vector3 translationSnap = Vector3(1);
	static float rotateSnap = 5;
	static float scaleSnap = 0.1f;
	float* pSnapValue = &translationSnap.x;

	switch (mCurrentGizmoOperation)
	{
	case ImGuizmo::TRANSLATE:
		ImGui::InputFloat3("Snap", &translationSnap.x);
		pSnapValue = &translationSnap.x;
		break;
	case ImGuizmo::ROTATE:
		ImGui::InputFloat("Angle Snap", &rotateSnap);
		pSnapValue = &rotateSnap;
		break;
	case ImGuizmo::SCALE:
		ImGui::InputFloat("Scale Snap", &scaleSnap);
		pSnapValue = &scaleSnap;
		break;
	}

	ImGuiIO& io = ImGui::GetIO();
	ImGuizmo::SetRect(0, 0, (float)io.DisplaySize.x, (float)io.DisplaySize.y);
	Matrix view = camera.GetView();
	Matrix projection = camera.GetProjection();
	Math::ReverseZProjection(projection);
	ImGuizmo::Manipulate(&view.m[0][0], &projection.m[0][0], mCurrentGizmoOperation, mCurrentGizmoMode, &matrix.m[0][0], nullptr, pSnapValue);
}

Matrix spotMatrix = Matrix::CreateScale(100.0f, 0.2f, 1) * Matrix::CreateFromYawPitchRoll(0.1f, 0, 0) * Matrix::CreateTranslation(0, 10, 0);

void Graphics::Update()
{
	PROFILE_BEGIN("Update");
	BeginFrame();
	m_pImGuiRenderer->Update();

	PROFILE_BEGIN("UpdateGameState");

	m_pShaderManager->ConditionallyReloadShaders();

	for (Batch& b : m_SceneData.Batches)
	{
		b.LocalBounds.Transform(b.Bounds, b.WorldMatrix);
	}

	EditTransform(*m_pCamera, spotMatrix);
	Vector3 scale, position;
	Quaternion rotation;
	spotMatrix.Decompose(scale, rotation, position);
	m_Lights[1].Range = scale.x;
	m_Lights[1].Position = position;
	m_Lights[1].Direction = spotMatrix.Forward();

	m_pCamera->Update();

	float costheta = cosf(Tweakables::g_SunOrientation);
	float sintheta = sinf(Tweakables::g_SunOrientation);
	float cosphi = cosf(Tweakables::g_SunInclination * Math::PIDIV2);
	float sinphi = sinf(Tweakables::g_SunInclination * Math::PIDIV2);
	m_Lights[0].Direction = -Vector3(costheta * sinphi, cosphi, sintheta * sinphi);
	m_Lights[0].Colour = Math::MakeFromColorTemperature(Tweakables::g_SunTemperature);
	m_Lights[0].Intensity = Tweakables::g_SunIntensity;

	std::sort(m_SceneData.Batches.begin(), m_SceneData.Batches.end(), [this](const Batch& a, const Batch& b) {
		if (a.BlendMode == b.BlendMode)
		{
			float aDist = Vector3::DistanceSquared(a.pMesh->Bounds.Center, m_pCamera->GetPosition());
			float bDist = Vector3::DistanceSquared(b.pMesh->Bounds.Center, m_pCamera->GetPosition());
			if (a.BlendMode == Batch::Blending::AlphaBlend)
			{
				return aDist > bDist;
			}
			return aDist < bDist;
		}
		return a.BlendMode < b.BlendMode;
	});

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
		Vector2* pData = (Vector2*)pSourceBuffer->GetMappedData();
		minPoint = pData->x;
		maxPoint = pData->y;
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
			Matrix projection = Math::CreatePerspectiveMatrix(light.UmbraAngleDegrees * Math::DegreesToRadians, 1.0f, light.Range, 1.0f);
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
			RegisterBindlessResource(pShadowMap.get());
		}
	}

	for (Light& light : m_Lights)
	{
		if (light.ShadowIndex >= 0)
		{
			light.ShadowMapSize = m_ShadowMaps[light.ShadowIndex]->GetWidth();
		}
	}

	shadowData.ShadowMapOffset = RegisterBindlessResource(m_ShadowMaps[0].get());
	m_SceneData.pDepthBuffer = GetDepthStencil();
	m_SceneData.pResolvedDepth = GetResolveDepthStencil();
	m_SceneData.pRenderTarget = GetCurrentRenderTarget();
	m_SceneData.pResolvedTarget = Tweakables::g_TAA ? m_pTAASource.get() : m_pHDRRenderTarget.get();
	m_SceneData.pPreviousColor = m_pPreviousColor.get();
	m_SceneData.pAO = m_pAmbientOcclusion.get();
	m_SceneData.pLightBuffer = m_pLightBuffer.get();
	m_SceneData.pCamera = m_pCamera.get();
	m_SceneData.pShadowData = &shadowData;
	m_SceneData.FrameIndex = m_Frame;
	m_SceneData.SceneTLAS = RegisterBindlessResource(m_pTLAS ? m_pTLAS->GetSRV() : nullptr);
	m_SceneData.pNormals = m_pNormals.get();
	m_SceneData.pResolvedNormals = m_pResolvedNormals.get();

	BoundingFrustum frustum = m_pCamera->GetFrustum();
	for (const Batch& b : m_SceneData.Batches)
	{
		m_SceneData.VisibilityMask.AssignBit(b.Index, frustum.Contains(b.Bounds));
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
				m_pScreenshotBuffer->Map();

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
				char* pData = (char*)m_pScreenshotBuffer->GetMappedData();
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

				SYSTEMTIME time;
				GetSystemTime(&time);
				char stringTarget[128];
				GetTimeFormat(LOCALE_INVARIANT, TIME_FORCE24HOURFORMAT, &time, "hh_mm_ss", stringTarget, 128);

				char filePath[256];
				Paths::CreateDirectoryTree(Paths::ScreenshotDir());
				sprintf_s(filePath, "%sScreenshot_%1s.jpg", Paths::ScreenshotDir().c_str(), stringTarget);
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

			renderContext.SetGraphicsRootSignature(m_pDepthPrepassRS.get());
			
			renderContext.BindResourceTable(2, m_SceneData.GlobalSRVHeapHandle.GpuHandle, CommandListContext::Graphics);

			struct ViewData
			{
				Matrix ViewProjection;
			} viewData{};
			viewData.ViewProjection = m_pCamera->GetViewProjection();
			renderContext.SetGraphicsDynamicConstantBufferView(1, viewData);
			
			auto DrawBatches = [&](Batch::Blending blendMode)
			{
				struct PerObjectData
				{
					Matrix World;
					MaterialData Material;
					uint32_t VertexBuffer;
				} objectData;

				for (const Batch& b : m_SceneData.Batches)
				{
					if (Any(b.BlendMode, blendMode) && m_SceneData.VisibilityMask.GetBit(b.Index))
					{
						objectData.World = b.WorldMatrix;
						objectData.Material = b.Material;
						objectData.VertexBuffer = b.VertexBufferDescriptor;
						renderContext.SetGraphicsDynamicConstantBufferView(0, objectData);
						renderContext.SetIndexBuffer(b.pMesh->IndicesLocation);
						renderContext.DrawIndexed(b.pMesh->IndicesLocation.Elements, 0, 0);
					}
				}
			};

			{
				GPU_PROFILE_SCOPE("Opaque", &renderContext);
				renderContext.SetPipelineState(m_pDepthPrepassOpaquePSO);
				DrawBatches(Batch::Blending::Opaque);
			}
			{
				GPU_PROFILE_SCOPE("Masked", &renderContext);
				renderContext.SetPipelineState(m_pDepthPrepassAlphaMaskPSO);
				DrawBatches(Batch::Blending::AlphaMask);
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
				renderContext.SetPipelineState(m_pResolveDepthPSO);

				renderContext.BindResource(0, 0, pDepthStencilResolve->GetUAV());
				renderContext.BindResource(1, 0, pDepthStencil->GetSRV());

				int dispatchGroupX = Math::DivideAndRoundUp(m_WindowWidth, 16);
				int dispatchGroupY = Math::DivideAndRoundUp(m_WindowHeight, 16);
				renderContext.Dispatch(dispatchGroupX, dispatchGroupY);

				renderContext.InsertResourceBarrier(pDepthStencilResolve, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
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
				renderContext.SetPipelineState(m_pCameraMotionPSO);

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

				renderContext.SetComputeDynamicConstantBufferView(0, parameters);

				renderContext.BindResource(1, 0, m_pVelocity->GetUAV());
				renderContext.BindResource(2, 0, GetResolveDepthStencil()->GetSRV());

				int dispatchGroupX = Math::DivideAndRoundUp(m_WindowWidth, 8);
				int dispatchGroupY = Math::DivideAndRoundUp(m_WindowHeight, 8);
				renderContext.Dispatch(dispatchGroupX, dispatchGroupY);
			});
	}

	m_pGpuParticles->Simulate(graph, GetResolveDepthStencil(), *m_pCamera);

	if (Tweakables::g_RaytracedAO)
	{
		m_pRTAO->Execute(graph, m_pAmbientOcclusion.get(), GetResolveDepthStencil(), m_SceneData, *m_pCamera);
	}
	else
	{
		m_pSSAO->Execute(graph, m_pAmbientOcclusion.get(), GetResolveDepthStencil(), *m_pCamera);
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
					renderContext.SetPipelineState(pDepthStencil->GetDesc().SampleCount > 1 ? m_pPrepareReduceDepthMsaaPSO : m_pPrepareReduceDepthPSO);

					struct ShaderParameters
					{
						float Near;
						float Far;
					} parameters;
					parameters.Near = m_pCamera->GetNear();
					parameters.Far = m_pCamera->GetFar();

					renderContext.SetComputeDynamicConstantBufferView(0, parameters);
					renderContext.BindResource(1, 0, m_ReductionTargets[0]->GetUAV());
					renderContext.BindResource(2, 0, pDepthStencil->GetSRV());

					renderContext.Dispatch(m_ReductionTargets[0]->GetWidth(), m_ReductionTargets[0]->GetHeight());

					renderContext.SetPipelineState(m_pReduceDepthPSO);
					for (size_t i = 1; i < m_ReductionTargets.size(); i++)
					{
						renderContext.InsertResourceBarrier(m_ReductionTargets[i - 1].get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
						renderContext.InsertResourceBarrier(m_ReductionTargets[i].get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

						renderContext.BindResource(1, 0, m_ReductionTargets[i]->GetUAV());
						renderContext.BindResource(2, 0, m_ReductionTargets[i - 1]->GetSRV());

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
					context.SetGraphicsDynamicConstantBufferView(1, viewData);
					context.BindResourceTable(2, m_SceneData.GlobalSRVHeapHandle.GpuHandle, CommandListContext::Graphics);

					struct PerObjectData
					{
						Matrix World;
						MaterialData Material;
						uint32_t VertexBuffer;
					} objectData;

					auto DrawBatches = [&](const Batch::Blending blendModes)
					{
						for (const Batch& b : m_SceneData.Batches)
						{
							if (Any(b.BlendMode, blendModes) && m_SceneData.VisibilityMask.GetBit(b.Index))
							{
								objectData.World = b.WorldMatrix;
								objectData.Material = b.Material;
								objectData.VertexBuffer = b.VertexBufferDescriptor;
								context.SetGraphicsDynamicConstantBufferView(0, objectData);
								context.SetIndexBuffer(b.pMesh->IndicesLocation);
								context.DrawIndexed(b.pMesh->IndicesLocation.Elements, 0, 0);
							}
						}
					};

					{
						GPU_PROFILE_SCOPE("Opaque", &context);
						context.SetPipelineState(m_pShadowOpaquePSO);
						DrawBatches(Batch::Blending::Opaque);
					}
					{
						GPU_PROFILE_SCOPE("Masked", &context);
						context.SetPipelineState(m_pShadowAlphaMaskPSO);
						DrawBatches(Batch::Blending::AlphaMask);
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

			renderContext.InsertResourceBarrier(pDepthStencil, D3D12_RESOURCE_STATE_DEPTH_WRITE);
			renderContext.InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET);

			RenderPassInfo info = RenderPassInfo(GetCurrentRenderTarget(), RenderPassAccess::Load_Store, pDepthStencil, RenderPassAccess::Load_Store, false);

			renderContext.BeginRenderPass(info);
			renderContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			renderContext.SetPipelineState(m_pSkyboxPSO);
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

			renderContext.SetGraphicsDynamicConstantBufferView(0, constBuffer);

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

	if (Tweakables::g_RaytracedReflections)
	{
		m_pRTReflections->Execute(graph, m_SceneData);
	}

	if (Tweakables::g_TAA)
	{
		// temporal resolve
		RGPassBuilder temporalResolve = graph.AddPass("Temporal Resolve");
		temporalResolve.Bind([=](CommandContext& context, const RGPassResource& resources)
			{
				context.InsertResourceBarrier(m_pTAASource.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				context.InsertResourceBarrier(m_pHDRRenderTarget.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(m_pVelocity.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(m_pPreviousColor.get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);

				context.SetComputeRootSignature(m_pTemporalResolveRS.get());
				context.SetPipelineState(m_pTemporalResolvePSO);

				struct TemporalParameters
				{
					Vector2 InvScreenDimensions;
					Vector2 Jitter;
				} parameters;

				parameters.InvScreenDimensions = Vector2(1.0f / m_WindowWidth, 1.0f / m_WindowHeight);
				parameters.Jitter.x = m_pCamera->GetPrevJitter().x - m_pCamera->GetJitter().x;
				parameters.Jitter.y = -(m_pCamera->GetPrevJitter().y - m_pCamera->GetJitter().y);
				context.SetComputeDynamicConstantBufferView(0, parameters);

				context.BindResource(1, 0, m_pHDRRenderTarget->GetUAV());
				context.BindResource(2, 0, m_pVelocity->GetSRV());
				context.BindResource(2, 1, m_pPreviousColor->GetSRV());
				context.BindResource(2, 2, m_pTAASource->GetSRV());
				context.BindResource(2, 3, GetResolveDepthStencil()->GetSRV());

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

					context.SetPipelineState(m_pGenerateMipsPSO);
					context.SetComputeRootSignature(m_pGenerateMipsRS.get());

					struct DownscaleParameters
					{
						IntVector2 TargetDimensions;
						Vector2 TargetDimensionsInv;
					} parameters{};

					parameters.TargetDimensions.x = pTonemapInput->GetWidth();
					parameters.TargetDimensions.y = pTonemapInput->GetHeight();
					parameters.TargetDimensionsInv = Vector2(1.0f / pTonemapInput->GetWidth(), 1.0f / pTonemapInput->GetHeight());

					context.SetComputeDynamicConstantBufferView(0, parameters);
					context.BindResource(1, 0, pTonemapInput->GetUAV());
					context.BindResource(2, 0, m_pHDRRenderTarget->GetSRV());

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
				context.InsertResourceBarrier(pTonemapInput, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);

				context.ClearUavUInt(m_pLuminanceHistogram.get(), m_pLuminanceHistogram->GetUAV());

				context.SetPipelineState(m_pLuminanceHistogramPSO);
				context.SetComputeRootSignature(m_pLuminanceHistogramRS.get());

				struct HistogramParameters
				{
					uint32_t Width;
					uint32_t Height;
					float MinLogLuminance;
					float OneOverLogLuminanceRange;
				} parameters;

				parameters.Width = pTonemapInput->GetWidth();
				parameters.Height = pTonemapInput->GetHeight();
				parameters.MinLogLuminance = Tweakables::g_MinLogLuminance;
				parameters.OneOverLogLuminanceRange = 1.0f / (Tweakables::g_MaxLogLuminance - Tweakables::g_MinLogLuminance);

				context.SetComputeDynamicConstantBufferView(0, parameters);
				context.BindResource(1, 0, m_pLuminanceHistogram->GetUAV());
				context.BindResource(2, 0, pTonemapInput->GetSRV());

				context.Dispatch(Math::DivideAndRoundUp(pTonemapInput->GetWidth(), 16), Math::DivideAndRoundUp(pTonemapInput->GetHeight(), 16));
			});

		// average luminance, compute the average luminance from the histogram
		RGPassBuilder averageLuminance = graph.AddPass("Average Luminance");
		averageLuminance.Bind([=](CommandContext& context, const RGPassResource& resources)
			{
				context.InsertResourceBarrier(m_pLuminanceHistogram.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(m_pAverageLuminance.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				context.SetPipelineState(m_pAverageLuminancePSO);
				context.SetComputeRootSignature(m_pAverageLuminanceRS.get());

				struct AverageLuminanceParameters
				{
					uint32_t PixelCount;
					float MinLogLuminance;
					float LogLuminanceRange;
					float TimeDelta;
					float Tau;
				} averageParameters;

				averageParameters.PixelCount = pTonemapInput->GetWidth() * pTonemapInput->GetHeight();
				averageParameters.MinLogLuminance = Tweakables::g_MinLogLuminance;
				averageParameters.LogLuminanceRange = Tweakables::g_MaxLogLuminance - Tweakables::g_MinLogLuminance;
				averageParameters.TimeDelta = Time::DeltaTime();
				averageParameters.Tau = Tweakables::g_Tau;

				context.SetComputeDynamicConstantBufferView(0, averageParameters);
				context.BindResource(1, 0, m_pAverageLuminance->GetUAV());
				context.BindResource(2, 0, m_pLuminanceHistogram->GetSRV());

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

				context.SetPipelineState(m_pToneMapPSO);
				context.SetComputeRootSignature(m_pToneMapRS.get());

				context.SetComputeDynamicConstantBufferView(0, constBuffer);
				context.BindResource(1, 0, m_pTonemapTarget->GetUAV());
				context.BindResource(2, 0, m_pHDRRenderTarget->GetSRV());
				context.BindResource(2, 1, m_pAverageLuminance->GetSRV());

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

					context.SetPipelineState(m_pDrawHistogramPSO);
					context.SetComputeRootSignature(m_pDrawHistogramRS.get());

					struct AverageParameters
					{
						float MinLogLuminance{ 0 };
						float InverseLogLuminanceRange{ 0 };
					} parameters;

					parameters.MinLogLuminance = Tweakables::g_MinLogLuminance;
					parameters.InverseLogLuminanceRange = 1.0f / (Tweakables::g_MaxLogLuminance - Tweakables::g_MinLogLuminance);

					context.SetComputeDynamicConstantBufferView(0, parameters);
					context.BindResource(1, 0, m_pTonemapTarget->GetUAV());
					context.BindResource(2, 0, m_pLuminanceHistogram->GetSRV());
					context.BindResource(2, 1, m_pAverageLuminance->GetSRV());

					context.Dispatch(1, m_pLuminanceHistogram->GetNumElements());
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
		m_pImGuiRenderer->Render(graph, m_SceneData, m_pTonemapTarget.get());
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
	//UnregisterWait(m_DeviceRemovedEvent);

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

	bool allowTearing = true;
	HRESULT hr = m_pFactory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));
	allowTearing &= SUCCEEDED(hr);

	bool setStablePowerState = CommandLine::GetBool("stablepowerstate");
	if (setStablePowerState)
	{
		D3D12EnableExperimentalFeatures(0, nullptr, nullptr, nullptr);
	}

	ComPtr<IDXGIAdapter4> pAdapter;

	bool useWarpAdapter = CommandLine::GetBool("warp");
	if (!useWarpAdapter)
	{
		uint32_t adapterIndex = 0;
		E_LOG(Info, "Adapters:");
		DXGI_GPU_PREFERENCE gpuPreference = DXGI_GPU_PREFERENCE_UNSPECIFIED;
		while (m_pFactory->EnumAdapterByGpuPreference(adapterIndex++, gpuPreference, IID_PPV_ARGS(pAdapter.ReleaseAndGetAddressOf())) == S_OK)
		{
			DXGI_ADAPTER_DESC3 desc;
			pAdapter->GetDesc3(&desc);
			E_LOG(Info, "\t%s - %f GB", UNICODE_TO_MULTIBYTE(desc.Description), (float)desc.DedicatedVideoMemory * Math::BytesToGigaBytes);

			uint32_t outputIndex = 0;
			ComPtr<IDXGIOutput> pOutput;
			while(pAdapter->EnumOutputs(outputIndex++, pOutput.ReleaseAndGetAddressOf()) == S_OK)
			{
				ComPtr<IDXGIOutput6> pOutput1;
				pOutput.As(&pOutput1);
				DXGI_OUTPUT_DESC1 outputDesc;
				pOutput1->GetDesc1(&outputDesc);

				E_LOG(Info, "\t\tMonitor %d - %dx%d - HDR: %s - %d BPP",
					outputIndex,
					outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left,
					outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top,
					outputDesc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ? "Yes" : "No",
					outputDesc.BitsPerColor);
			}
		}
		m_pFactory->EnumAdapterByGpuPreference(0, gpuPreference, IID_PPV_ARGS(pAdapter.ReleaseAndGetAddressOf()));
		DXGI_ADAPTER_DESC3 desc;
		pAdapter->GetDesc3(&desc);
		E_LOG(Info, "Using %s", UNICODE_TO_MULTIBYTE(desc.Description));

		// device
		constexpr D3D_FEATURE_LEVEL featureLevels[] =
		{
			D3D_FEATURE_LEVEL_12_2,
			D3D_FEATURE_LEVEL_12_1,
			D3D_FEATURE_LEVEL_12_0,
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0
		};

		auto GetFeatureLevelName = [](D3D_FEATURE_LEVEL featureLevel) {
			switch(featureLevel)
			{
			case D3D_FEATURE_LEVEL_12_2: return "D3D_FEATURE_LEVEL_12_2";
			case D3D_FEATURE_LEVEL_12_1: return "D3D_FEATURE_LEVEL_12_1";
			case D3D_FEATURE_LEVEL_12_0 : return "D3D_FEATURE_LEVEL_12_0";
			case D3D_FEATURE_LEVEL_11_1 : return "D3D_FEATURE_LEVEL_11_1";
			case D3D_FEATURE_LEVEL_11_0 : return "D3D_FEATURE_LEVEL_11_0";
			default: noEntry(); return "";
			}
		};

		VERIFY_HR(D3D12CreateDevice(pAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_pDevice)));
		D3D12_FEATURE_DATA_FEATURE_LEVELS caps = {
			.NumFeatureLevels = std::size(featureLevels),
			.pFeatureLevelsRequested = featureLevels,
		};
		VERIFY_HR_EX(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &caps, sizeof(D3D12_FEATURE_DATA_FEATURE_LEVELS)), GetDevice());
		VERIFY_HR_EX(D3D12CreateDevice(pAdapter.Get(), caps.MaxSupportedFeatureLevel, IID_PPV_ARGS(m_pDevice.ReleaseAndGetAddressOf())), GetDevice());
		E_LOG(Info, "D3D12 Device Created: %s", GetFeatureLevelName(caps.MaxSupportedFeatureLevel));
	}

	if (!m_pDevice)
	{
		E_LOG(Warning, "No D3D12 Adapter selected. Falling back to WARP");
		m_pFactory->EnumWarpAdapter(IID_PPV_ARGS(pAdapter.GetAddressOf()));
		VERIFY_HR(D3D12CreateDevice(pAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(m_pDevice.ReleaseAndGetAddressOf())));
	}

	m_pDevice.As(&m_pRaytracingDevice);
	D3D::SetObjectName(m_pDevice.Get(), "Main Device");

	/*auto OnDeviceRemovedCallback = [](void* pContext, BOOLEAN) {
		Graphics* pGraphics = (Graphics*)pContext;
		std::string error = D3D::GetErrorString(DXGI_ERROR_DEVICE_REMOVED, pGraphics->GetDevice());
		E_LOG(Error, "%s", error.c_str());
	};

	HANDLE deviceRemovedEvent = CreateEventA(nullptr, false, false, nullptr);
	VERIFY_HR(m_pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_pDeviceRemovalFence.GetAddressOf())));
	D3D::SetObjectName(m_pDeviceRemovalFence.Get(), "Device Removal Fence");
	m_pDeviceRemovalFence->SetEventOnCompletion(UINT64_MAX, deviceRemovedEvent);
	RegisterWaitForSingleObject(&deviceRemovedEvent, deviceRemovedEvent, OnDeviceRemovedCallback, this, INFINITE, 0);*/

	if (setStablePowerState)
	{
		VERIFY_HR(m_pDevice->SetStablePowerState(true));
	}

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
		D3D12_FEATURE_DATA_D3D12_OPTIONS caps0{};
		if (SUCCEEDED(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &caps0, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS))))
		{
			// level for placing different types of resources in the same heap.
			checkf(caps0.ResourceHeapTier >= D3D12_RESOURCE_HEAP_TIER_1, "device does not support Resource Heap Tier 2 or higher. Tier 1 is not supported");
			checkf(caps0.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3, "device does not support Resource Binding Tier 3 or higher. Tier 2 and under is not supported");
		}

		D3D12_FEATURE_DATA_D3D12_OPTIONS5 caps5{};
		if (SUCCEEDED(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &caps5, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS5))))
		{
			m_RenderPassTier = caps5.RenderPassesTier;
			m_RayTracingTier = caps5.RaytracingTier;
		}

		D3D12_FEATURE_DATA_D3D12_OPTIONS6 caps6{};
		if (SUCCEEDED(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &caps6, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS6))))
		{
			m_VSRTier = caps6.VariableShadingRateTier;
			m_VSRTileSize = caps6.ShadingRateImageTileSize;
		}

		D3D12_FEATURE_DATA_D3D12_OPTIONS7 caps7{};
		if (SUCCEEDED(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &caps7, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS7))))
		{
			m_MeshShaderSupport = caps7.MeshShaderTier;
			m_SamplerFeedbackSupport = caps7.SamplerFeedbackTier;
		}

		D3D12_FEATURE_DATA_SHADER_MODEL shaderModelSupport = {
			.HighestShaderModel = D3D_SHADER_MODEL_6_6
		};
		if (SUCCEEDED(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModelSupport, sizeof(D3D12_FEATURE_DATA_SHADER_MODEL))))
		{
			m_ShaderModelMajor = (uint8_t)(shaderModelSupport.HighestShaderModel >> 0x4);
			m_ShaderModelMinor = (uint8_t)(shaderModelSupport.HighestShaderModel & 0xF);

			E_LOG(Info, "D3D12 Shader Model %d.%d", m_ShaderModelMajor, m_ShaderModelMinor);
		}
	}

	// create all the required command queues
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_COMPUTE] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_COMPUTE);
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_COPY] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_COPY);

	m_pDynamicAllocationManager = std::make_unique<DynamicAllocationManager>(this, BufferFlag::Upload);

	m_pGlobalViewHeap = std::make_unique<GlobalOnlineDescriptorHeap>(this, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2000, 1000000);
	m_pPersistentDescriptorHeap = std::make_unique<OnlineDescriptorAllocator>(m_pGlobalViewHeap.get());
	m_SceneData.GlobalSRVHeapHandle = m_pGlobalViewHeap->GetStartHandle();

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

	m_pShaderManager = std::make_unique<ShaderManager>("Resources/Shaders/", m_ShaderModelMajor, m_ShaderModelMinor);

	m_pImGuiRenderer = std::make_unique<ImGuiRenderer>(this);
	m_pImGuiRenderer->AddUpdateCallback(ImGuiCallbackDelegate::CreateRaw(this, &Graphics::UpdateImGui));

	m_pNormals = std::make_unique<GraphicsTexture>(this, "MSAA Normals");
	m_pResolvedNormals = std::make_unique<GraphicsTexture>(this, "Resolved Normals");
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

	m_pNormals->Create(TextureDesc::CreateRenderTarget(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, TextureFlag::RenderTarget, m_SampleCount));
	m_pResolvedNormals->Create(TextureDesc::CreateRenderTarget(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, TextureFlag::ShaderResource | TextureFlag::RenderTarget));
	m_pHDRRenderTarget->Create(TextureDesc::CreateRenderTarget(width, height, RENDER_TARGET_FORMAT, TextureFlag::ShaderResource | TextureFlag::RenderTarget | TextureFlag::UnorderedAccess));
	m_pPreviousColor->Create(TextureDesc::Create2D(width, height, RENDER_TARGET_FORMAT, TextureFlag::ShaderResource));
	m_pTonemapTarget->Create(TextureDesc::CreateRenderTarget(width, height, SWAPCHAIN_FORMAT, TextureFlag::ShaderResource | TextureFlag::RenderTarget | TextureFlag::UnorderedAccess));
	m_pDownscaledColor->Create(TextureDesc::Create2D(Math::DivideAndRoundUp(width, 4), Math::DivideAndRoundUp(height, 4), RENDER_TARGET_FORMAT, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource));
	m_pVelocity->Create(TextureDesc::Create2D(width, height, DXGI_FORMAT_R16G16_FLOAT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess));
	m_pTAASource->Create(TextureDesc::CreateRenderTarget(width, height, RENDER_TARGET_FORMAT, TextureFlag::ShaderResource | TextureFlag::RenderTarget | TextureFlag::UnorderedAccess));
	m_pAmbientOcclusion->Create(TextureDesc::Create2D(Math::DivideAndRoundUp(width, 2), Math::DivideAndRoundUp(height, 2), DXGI_FORMAT_R8_UNORM, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess));

	m_pCamera->SetViewport(FloatRect(0, 0, (float)width, (float)height));
	m_pCamera->SetDirty();

	m_pClusteredForward->OnSwapchainCreated(width, height);
	m_pTiledForward->OnSwapchainCreated(width, height);
	m_pSSAO->OnSwapchainCreated(width, height);
	m_pRTReflections->OnResize(width, height);

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
		pBuffer->Map();
		m_ReductionReadbackTargets.push_back(std::move(pBuffer));
	}
}

void Graphics::InitializeAssets(CommandContext& context)
{
	auto RegisterDefaultTexture = [this, &context](DefaultTexture type, const char* pName, const TextureDesc& desc, uint32_t* pData) {
		m_DefaultTextures[(int)type] = std::make_unique<GraphicsTexture>(this, "Default Black");
		m_DefaultTextures[(int)type]->Create(&context, desc, pData);

	};

	uint32_t BLACK = 0xFF000000;
	RegisterDefaultTexture(DefaultTexture::Black2D, "Default Black", TextureDesc::Create2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM), &BLACK);

	uint32_t WHITE = 0xFFFFFFFF;
	RegisterDefaultTexture(DefaultTexture::White2D, "Default White", TextureDesc::Create2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM), &WHITE);

	uint32_t MAGENTA = 0xFFFF00FF;
	RegisterDefaultTexture(DefaultTexture::Magenta2D, "Default Magenta", TextureDesc::Create2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM), &MAGENTA);

	uint32_t GRAY = 0xFF808080;
	RegisterDefaultTexture(DefaultTexture::Gray2D, "Default Gray", TextureDesc::Create2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM), &GRAY);

	uint32_t DEFAULT_NORMAL = 0xFFFF8080;
	RegisterDefaultTexture(DefaultTexture::Normal2D, "Default Normal", TextureDesc::Create2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM), &DEFAULT_NORMAL);
	
	uint32_t BLACK_CUBE[6] = {};
	RegisterDefaultTexture(DefaultTexture::BlackCube, "Default Black Cube", TextureDesc::CreateCube(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM), BLACK_CUBE);

	uint32_t DEFAULT_ROUGHNESS_METALNESS = 0xFF0080FF;
	RegisterDefaultTexture(DefaultTexture::RoughnessMetalness, "Default Roughness/Metalness", TextureDesc::Create2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM), &DEFAULT_ROUGHNESS_METALNESS);

	m_DefaultTextures[(int)DefaultTexture::ColorNoise256] = std::make_unique<GraphicsTexture>(this, "Color Noise 256px");
	m_DefaultTextures[(int)DefaultTexture::ColorNoise256]->Create(&context, "Resources/Textures/noise.png", false);
	m_DefaultTextures[(int)DefaultTexture::BlueNoise512] = std::make_unique<GraphicsTexture>(this, "Blue Noise 512px");
	m_DefaultTextures[(int)DefaultTexture::BlueNoise512]->Create(&context, "Resources/Textures/BlueNoise512.png", false);
}

void Graphics::SetupScene(CommandContext& context)
{
	m_pLightCookie = std::make_unique<GraphicsTexture>(this, "Light Cookie");
	m_pLightCookie->Create(&context, "Resources/Textures/LightProjector.png", false);

	{
		std::unique_ptr<Mesh> pMesh = std::make_unique<Mesh>();	
		pMesh->Load("Resources/sponza/sponza.dae", this, &context);
		m_Meshes.push_back(std::move(pMesh));
	}

	Matrix transforms[] = {
		Matrix::CreateTranslation(0, 0, 0),
	};

	for (uint32_t j = 0; j < (uint32_t)m_Meshes.size(); j++)
	{
		auto& pMesh = m_Meshes[j];
		for (const SubMeshInstance& node : pMesh->GetMeshInstances())
		{
			const SubMesh& subMesh = pMesh->GetMesh(node.MeshIndex);
			const Material& material = pMesh->GetMaterial(subMesh.MaterialId);
			m_SceneData.Batches.push_back(Batch{});
			Batch& b = m_SceneData.Batches.back();
			b.Index = (int)m_SceneData.Batches.size() - 1;
			b.LocalBounds = subMesh.Bounds;
			b.pMesh = &subMesh;
			b.WorldMatrix = transforms[j];
			b.VertexBufferDescriptor = RegisterBindlessResource(subMesh.pVertexSRV);
			b.IndexBufferDescriptor = RegisterBindlessResource(subMesh.pIndexSRV);

			b.Material.Diffuse = RegisterBindlessResource(material.pDiffuseTexture, GetDefaultTexture(DefaultTexture::White2D));
			b.Material.Normal = RegisterBindlessResource(material.pNormalTexture, GetDefaultTexture(DefaultTexture::Normal2D));
			b.Material.RoughnessMetalness = RegisterBindlessResource(material.pRoughnessMetalnessTexture, GetDefaultTexture(DefaultTexture::RoughnessMetalness));
			b.Material.Emissive = RegisterBindlessResource(material.pEmissiveTexture, GetDefaultTexture(DefaultTexture::Black2D));

			b.BlendMode = material.IsTransparent ? Batch::Blending::AlphaMask : Batch::Blending::Opaque;
		}
	}

	{
		Vector3 position(-150, 160, -10);
		Vector3 direction;
		position.Normalize(direction);

		Light sunLight = Light::Directional(position, -direction, 10.0f);
		sunLight.CastShadows = true;
		sunLight.VolumetricLighting = true;
		m_Lights.push_back(sunLight);

		{
			Light spotLight = Light::Spot(Vector3(-5, 16, 16), 800, Vector3(0, 1, 0), 90, 70, 1000, Color(1, 0.7f, 0.3f, 1.0f));
			spotLight.CastShadows = true;
			spotLight.LightTexture = RegisterBindlessResource(m_pLightCookie.get(), GetDefaultTexture(DefaultTexture::White2D));
			spotLight.VolumetricLighting = true;
			m_Lights.push_back(spotLight);
		}

		m_pLightBuffer = std::make_unique<Buffer>(this, "Lights");
		m_pLightBuffer->Create(BufferDesc::CreateStructured((uint32_t)m_Lights.size(), sizeof(Light::RenderData), BufferFlag::ShaderResource));
	}
}

void Graphics::InitializePipelines()
{
	// shadow mapping
	// vertex shader-only pass that writes to the depth buffer using the light matrix
	{
		Shader* pVertexShader = GetShaderManager()->GetShader("DepthOnly.hlsl", ShaderType::Vertex, "VSMain");
		Shader* pAlphaClipPixelShader = GetShaderManager()->GetShader("DepthOnly.hlsl", ShaderType::Pixel, "PSMain");

		// root signature
		m_pShadowRS = std::make_unique<RootSignature>(this);
		m_pShadowRS->FinalizeFromShader("Shadow Mapping RS", pVertexShader);

		// pipeline state
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pShadowRS->GetRootSignature());
		psoDesc.SetVertexShader(pVertexShader);
		psoDesc.SetRenderTargetFormats(nullptr, 0, DEPTH_STENCIL_SHADOW_FORMAT, 1);
		psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
		psoDesc.SetDepthBias(-1, -5.f, -4.f);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetName("Shadow Mapping Opaque PSO");
		m_pShadowOpaquePSO = CreatePipeline(psoDesc);
		
		psoDesc.SetPixelShader(pAlphaClipPixelShader);
		psoDesc.SetName("Shadow Mapping AlphaMask PSO");
		m_pShadowAlphaMaskPSO = CreatePipeline(psoDesc);
	}

	// depth prepass
	// simple vertex shader to fill the depth buffer to optimize later passes
	{
		Shader* pVertexShader = GetShaderManager()->GetShader("DepthOnly.hlsl", ShaderType::Vertex, "VSMain");
		Shader* pixelShader = GetShaderManager()->GetShader("DepthOnly.hlsl", ShaderType::Pixel, "PSMain");

		// root signature
		m_pDepthPrepassRS = std::make_unique<RootSignature>(this);
		m_pDepthPrepassRS->FinalizeFromShader("Depth Prepass RS", pVertexShader);

		// pipeline state
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pDepthPrepassRS->GetRootSignature());
		psoDesc.SetVertexShader(pVertexShader);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetRenderTargetFormats(nullptr, 0, DEPTH_STENCIL_FORMAT, m_SampleCount);
		psoDesc.SetName("Depth Prepass Opaque PSO");
		m_pDepthPrepassOpaquePSO = CreatePipeline(psoDesc);

		psoDesc.SetPixelShader(pixelShader);
		psoDesc.SetName("Depth Prepass AlphaMask PSO");
		m_pDepthPrepassAlphaMaskPSO = CreatePipeline(psoDesc);
	}

	// luminance histogram
	{
		Shader* pComputeShader = GetShaderManager()->GetShader("Tonemap/LuminanceHistogram.hlsl", ShaderType::Compute, "CSMain");

		// root signature
		m_pLuminanceHistogramRS = std::make_unique<RootSignature>(this);
		m_pLuminanceHistogramRS->FinalizeFromShader("Luminance Histogram RS", pComputeShader);

		// pipeline state
		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pLuminanceHistogramRS->GetRootSignature());
		psoDesc.SetName("Luminance Histogram PSO");
		m_pLuminanceHistogramPSO = CreatePipeline(psoDesc);

		m_pLuminanceHistogram = std::make_unique<Buffer>(this, "Luminance Histogram");
		m_pLuminanceHistogram->Create(BufferDesc::CreateByteAddress(sizeof(uint32_t) * 256));
		m_pAverageLuminance = std::make_unique<Buffer>(this, "Average Luminance");
		m_pAverageLuminance->Create(BufferDesc::CreateStructured(3, sizeof(float), BufferFlag::UnorderedAccess | BufferFlag::ShaderResource));
	}

	// draw histogram
	{
		Shader* pComputeShader = GetShaderManager()->GetShader("Tonemap/DrawLuminanceHistogram.hlsl", ShaderType::Compute, "DrawLuminanceHistogram");
		m_pDrawHistogramRS = std::make_unique<RootSignature>(this);
		m_pDrawHistogramRS->FinalizeFromShader("Draw Histogram RS", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pDrawHistogramRS->GetRootSignature());
		psoDesc.SetName("Draw Histogram PSO");
		m_pDrawHistogramPSO = CreatePipeline(psoDesc);
	}

	// average luminance
	{
		Shader* pComputeShader = GetShaderManager()->GetShader("Tonemap/AverageLuminance.hlsl", ShaderType::Compute, "CSMain");

		// root signature
		m_pAverageLuminanceRS = std::make_unique<RootSignature>(this);
		m_pAverageLuminanceRS->FinalizeFromShader("Average Luminance RS", pComputeShader);

		// pipeline state
		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pAverageLuminanceRS->GetRootSignature());
		psoDesc.SetName("Average Luminance PSO");
		m_pAverageLuminancePSO = CreatePipeline(psoDesc);
	}

	// camera motion
	{
		Shader* pComputeShader = GetShaderManager()->GetShader("CameraMotionVectors.hlsl", ShaderType::Compute, "CSMain");

		m_pCameraMotionRS = std::make_unique<RootSignature>(this);
		m_pCameraMotionRS->FinalizeFromShader("Camera Motion RS", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pCameraMotionRS->GetRootSignature());
		psoDesc.SetName("Camera Motion PSO");
		m_pCameraMotionPSO = CreatePipeline(psoDesc);
	}

	// tonemapping
	{
		Shader* pComputeShader = GetShaderManager()->GetShader("Tonemap/Tonemapping.hlsl", ShaderType::Compute, "CSMain");

		// rootSignature
		m_pToneMapRS = std::make_unique<RootSignature>(this);
		m_pToneMapRS->FinalizeFromShader("Tonemapping RS", pComputeShader);

		// pipeline state
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pToneMapRS->GetRootSignature());
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetName("Tonemapping PSO");
		m_pToneMapPSO = CreatePipeline(psoDesc);
	}

	// depth resolve
	// resolves a multisampled buffer to a normal depth buffer
	// only required when the sample count > 1
	{
		Shader* pComputeShader = GetShaderManager()->GetShader("ResolveDepth.hlsl", ShaderType::Compute, "CSMain");

		m_pResolveDepthRS = std::make_unique<RootSignature>(this);
		m_pResolveDepthRS->FinalizeFromShader("Resolve Depth RS", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pResolveDepthRS->GetRootSignature());
		psoDesc.SetName("Resolve Depth Pipeline");
		m_pResolveDepthPSO = CreatePipeline(psoDesc);
	}

	// depth reduce
	{
		Shader* pPrepareReduceShader = GetShaderManager()->GetShader("ReduceDepth.hlsl", ShaderType::Compute, "PrepareReduceDepth");
		Shader* pPrepareReduceShaderMSAA = GetShaderManager()->GetShader("ReduceDepth.hlsl", ShaderType::Compute, "PrepareReduceDepth", { "WITH_MSAA" });
		Shader* pReduceShader = GetShaderManager()->GetShader("ReduceDepth.hlsl", ShaderType::Compute, "ReduceDepth");

		m_pReduceDepthRS = std::make_unique<RootSignature>(this);
		m_pReduceDepthRS->FinalizeFromShader("Reduce Depth RS", pReduceShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pPrepareReduceShader);
		psoDesc.SetRootSignature(m_pReduceDepthRS->GetRootSignature());
		psoDesc.SetName("Prepare Reduce Depth PSO");
		m_pPrepareReduceDepthPSO = CreatePipeline(psoDesc);

		psoDesc.SetComputeShader(pPrepareReduceShaderMSAA);
		psoDesc.SetName("Prepare Reduce Depth MSAA PSO");
		m_pPrepareReduceDepthMsaaPSO = CreatePipeline(psoDesc);

		psoDesc.SetComputeShader(pReduceShader);
		psoDesc.SetName("Reduce Depth PSO");
		m_pReduceDepthPSO = CreatePipeline(psoDesc);
	}

	//TAA
	{
		Shader* pComputeShader = GetShaderManager()->GetShader("TemporalResolve.hlsl", ShaderType::Compute, "CSMain");
		
		m_pTemporalResolveRS = std::make_unique<RootSignature>(this);
		m_pTemporalResolveRS->FinalizeFromShader("Temporal Resolve RS", pComputeShader);
		
		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pTemporalResolveRS->GetRootSignature());
		psoDesc.SetName("Temporal Resolve PSO");
		m_pTemporalResolvePSO = CreatePipeline(psoDesc);
	}

	// mip generation
	{
		Shader* pComputeShader = GetShaderManager()->GetShader("GenerateMips.hlsl", ShaderType::Compute, "CSMain");

		m_pGenerateMipsRS = std::make_unique<RootSignature>(this);
		m_pGenerateMipsRS->FinalizeFromShader("Generate Mips RS", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pGenerateMipsRS->GetRootSignature());
		psoDesc.SetName("Generate Mips PSO");
		m_pGenerateMipsPSO = CreatePipeline(psoDesc);
	}

	// sky
	{
		Shader* pVertexShader = GetShaderManager()->GetShader("ProceduralSky.hlsl", ShaderType::Vertex, "VSMain");
		Shader* pPixelShader = GetShaderManager()->GetShader("ProceduralSky.hlsl", ShaderType::Pixel, "PSMain");

		// root signature
		m_pSkyboxRS = std::make_unique<RootSignature>(this);
		m_pSkyboxRS->FinalizeFromShader("Skybox RS", pVertexShader);

		// pipeline state
		PipelineStateInitializer psoDesc;
		psoDesc.SetInputLayout(nullptr, 0);
		psoDesc.SetRootSignature(m_pSkyboxRS->GetRootSignature());
		psoDesc.SetVertexShader(pVertexShader);
		psoDesc.SetPixelShader(pPixelShader);
		psoDesc.SetRenderTargetFormat(RENDER_TARGET_FORMAT, DEPTH_STENCIL_FORMAT, m_SampleCount);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetName("Skybox");
		m_pSkyboxPSO = CreatePipeline(psoDesc);
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
			ImGui::ProgressBar((float)usedDescriptors / Math::Max(1u, totalDescriptors), ImVec2(-1, 0), str.str().c_str());
		}
		ImGui::TreePop();
	}
	if (ImGui::TreeNodeEx("Memory", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Text("Dynamic Upload Memory");
		ImGui::Text("%.2f MB", Math::BytesToMegaBytes * m_pDynamicAllocationManager->GetMemoryUsage());
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Profiler", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ProfileNode* pRootNode = Profiler::Get()->GetRootNode();
		pRootNode->RenderImGui(m_Frame);
		ImGui::TreePop();
	}

	ImGui::End();

	static bool showOutputLog = false;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
	ImGui::SetNextWindowPos(ImVec2(300, (float)m_WindowHeight), 0, ImVec2(0, 1));
	ImGui::SetNextWindowSize(ImVec2((float)m_WindowWidth - 300 * 2.0f, 250));
	ImGui::SetNextWindowCollapsed(!showOutputLog);

	showOutputLog = ImGui::Begin("Output Log", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	if (showOutputLog)
	{
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
		ImGui::SetScrollHereY(0.0f);
	}
	
	ImGui::PopStyleVar();
	ImGui::End();

	ImGui::SetNextWindowPos(ImVec2((float)m_WindowWidth, 0), 0, ImVec2(1, 0));
	ImGui::SetNextWindowSize(ImVec2(300, (float)m_WindowHeight));
	ImGui::Begin("Parameters", 0, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

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

	if (SupportsRaytracing())
	{
		ImGui::Checkbox("Raytraced AO", &Tweakables::g_RaytracedAO);
		ImGui::Checkbox("Raytraced Reflections", &Tweakables::g_RaytracedReflections);
	}

	ImGui::Checkbox("TAA", &Tweakables::g_TAA);

	ImGui::End();

	m_pVisualizeTexture = m_pVisualizeTexture != nullptr ? m_pVisualizeTexture : m_pAmbientOcclusion.get();
	if (m_pVisualizeTexture)
	{
		std::string tabName = std::string("Visualize Texture:") + m_pVisualizeTexture->GetName();
		ImGui::Begin(tabName.c_str());
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

		ImGui::Image(m_pVisualizeTexture, ImVec2(width, height));
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
			ImGui::Image(m_ShadowMaps[sunLight.ShadowIndex + i].get(), ImVec2(imageSize, imageSize));
		}
		ImGui::End();
	}
}

void Graphics::UpdateTLAS(CommandContext& context)
{
	if (!SupportsRaytracing())
	{
		return;
	}

	ID3D12GraphicsCommandList4* pCmd = context.GetRaytracingCommandList();

	bool isUpdate = m_pTLAS != nullptr;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

	// top level acceleration structure
	std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;
	for (uint32_t instanceIndex = 0; instanceIndex < (uint32_t)m_SceneData.Batches.size(); ++instanceIndex)
	{
		const Batch& batch = m_SceneData.Batches[instanceIndex];
		const SubMesh& subMesh = *batch.pMesh;
		D3D12_RAYTRACING_INSTANCE_DESC instanceDesc{};
		instanceDesc.AccelerationStructure = subMesh.pBLAS->GetGpuHandle();
		instanceDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		instanceDesc.InstanceContributionToHitGroupIndex = 0;
		instanceDesc.InstanceID = batch.Index;
		instanceDesc.InstanceMask = 0xFF;

		// the layout of Transform is a transpose of how affine matrices are typically stored in memory. 
		// Instead of four 3-vectors. Transform is laid out as three 4-vectors.
		auto ApplyTransform = [](const Matrix& m, D3D12_RAYTRACING_INSTANCE_DESC& desc)
			{
				Matrix transpose = m.Transpose();
				memcpy(&desc.Transform, &transpose, sizeof(float) * 12);
			};

		ApplyTransform(batch.WorldMatrix, instanceDesc);
		instanceDescs.push_back(instanceDesc);
	}

	if (!isUpdate)
	{
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS prebuildInfo = {
			.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
			.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
					D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION,
			.NumDescs = (uint32_t)instanceDescs.size(),
			.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
		};

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
		GetRaytracingDevice()->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfo, &info);

		m_pTLASScratch = std::make_unique<Buffer>(this, "TLAS Scratch");
		m_pTLASScratch->Create(BufferDesc::CreateByteAddress(Math::AlignUp<uint64_t>(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::None));
		m_pTLAS = std::make_unique<Buffer>(this, "TLAS");
		m_pTLAS->Create(BufferDesc::CreateAccelerationStructure(Math::AlignUp<uint64_t>(info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)));
	}

	DynamicAllocation allocation = context.AllocateTransientMemory(instanceDescs.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
	memcpy(allocation.pMappedMemory, instanceDescs.data(), instanceDescs.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC));

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {
		.DestAccelerationStructureData = m_pTLAS->GetGpuHandle(),
		.Inputs = {
			.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
			.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE |
					D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
			.NumDescs = (uint32_t)instanceDescs.size(),
			.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
			.InstanceDescs = allocation.GpuHandle,
		},
		.SourceAccelerationStructureData = 0,
		.ScratchAccelerationStructureData = m_pTLASScratch->GetGpuHandle(),
	};

	pCmd->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
	context.InsertUavBarrier(m_pTLAS.get());
}

CommandQueue* Graphics::GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const
{
	return m_CommandQueues.at(type).get();
}

void Graphics::FreeCommandList(CommandContext* pCommandList)
{
	std::lock_guard lockGuard(m_ContextAllocationMutex);
	m_FreeCommandLists[(int)pCommandList->GetType()].push(pCommandList);
}

CommandContext* Graphics::AllocateCommandContext(D3D12_COMMAND_LIST_TYPE type)
{
	int typeIndex = (int)type;
	CommandContext* pContext = nullptr;

	{
		std::scoped_lock lock(m_ContextAllocationMutex);
		if (m_FreeCommandLists[typeIndex].size() > 0)
		{
			pContext = m_FreeCommandLists[typeIndex].front();
			m_FreeCommandLists[typeIndex].pop();
			pContext->Reset();
		}
		else
		{
			ComPtr<ID3D12CommandList> pCommandList;
			ID3D12CommandAllocator* pAllocator = m_CommandQueues[type]->RequestAllocator();
			VERIFY_HR(m_pDevice->CreateCommandList(0, type, pAllocator, nullptr, IID_PPV_ARGS(pCommandList.GetAddressOf())));
			D3D::SetObjectName(pCommandList.Get(), Sprintf("Pooled Commandlist - %d", m_CommandLists.size()).c_str());
			m_CommandLists.push_back(std::move(pCommandList));
			m_CommandListPool[typeIndex].emplace_back(std::make_unique<CommandContext>(this, static_cast<ID3D12GraphicsCommandList*>(m_CommandLists.back().Get()), type, pAllocator));
			pContext = m_CommandListPool[typeIndex].back().get();
		}
	}
	return pContext;
}

void Graphics::WaitForFence(uint64_t fenceValue)
{
	D3D12_COMMAND_LIST_TYPE type = (D3D12_COMMAND_LIST_TYPE)(fenceValue >> 56);
	CommandQueue* pQueue = GetCommandQueue(type);
	pQueue->WaitForFence(fenceValue);
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
	VERIFY_HR_EX(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &featureData, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS)), GetDevice());

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
			VERIFY_HR_EX(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &formatSupport, sizeof(D3D12_FEATURE_DATA_FORMAT_SUPPORT)), GetDevice());
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
	return m_RenderPassTier >= D3D12_RENDER_PASS_TIER::D3D12_RENDER_PASS_TIER_0;
}

bool Graphics::IsFenceComplete(uint64_t fenceValue)
{
	D3D12_COMMAND_LIST_TYPE type = (D3D12_COMMAND_LIST_TYPE)(fenceValue >> 56);
	CommandQueue* pQueue = GetCommandQueue(type);
	return pQueue->IsFenceComplete(fenceValue);
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

PipelineState* Graphics::CreatePipeline(const PipelineStateInitializer& psoDesc)
{
	std::unique_ptr<PipelineState> pPipeline = std::make_unique<PipelineState>(this);
	pPipeline->Create(psoDesc);
	m_Pipelines.push_back(std::move(pPipeline));
	return m_Pipelines.back().get();
}

StateObject* Graphics::CreateStateObject(const StateObjectInitializer& stateDesc)
{
	std::unique_ptr<StateObject> pStateObject = std::make_unique<StateObject>(this);
	pStateObject->Create(stateDesc);
	m_StateObjects.push_back(std::move(pStateObject));
	return m_StateObjects.back().get();
}

int Graphics::RegisterBindlessResource(GraphicsTexture* pTexture, GraphicsTexture* pFallbackTexture)
{
	return RegisterBindlessResource(pTexture ? pTexture->GetSRV() : nullptr, pFallbackTexture ? pFallbackTexture->GetSRV() : nullptr);
}

int Graphics::RegisterBindlessResource(ResourceView* pResourceView, ResourceView* pFallback)
{
	auto it = m_ViewToDescriptorIndex.find(pResourceView);
	if (it != m_ViewToDescriptorIndex.end())
	{
		return it->second;
	}

	if (pResourceView)
	{
		DescriptorHandle handle = m_pPersistentDescriptorHeap->Allocate(1);
		m_pDevice->CopyDescriptorsSimple(1, handle.CpuHandle, pResourceView->GetDescriptor(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		m_ViewToDescriptorIndex[pResourceView] = handle.HeapIndex;
		return handle.HeapIndex;
	}

	return pFallback ? RegisterBindlessResource(pFallback) : 0;
}
