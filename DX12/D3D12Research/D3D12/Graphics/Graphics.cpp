#include "stdafx.h"
#include "Graphics.h"
#include "CommandAllocatorPool.h"
#include "CommandQueue.h"
#include "CommandContext.h"
#include "DescriptorAllocator.h"
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
#include "PersistentResourceAllocator.h"
#include "ClusteredForward.h"
#include "Scene/Camera.h"
#include "Clouds.h"
#include <DXProgrammableCapture.h>

bool gSortOpaqueMeshes = true;
bool gSortTransparentMeshes = true;

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
	m_pCamera->SetRotation(Quaternion::CreateFromYawPitchRoll(XM_PIDIV4, XM_PIDIV4, 0));
	m_pCamera->SetNewPlane(400.f);
	m_pCamera->SetFarPlane(2.f);
	m_pCamera->SetViewport(0, 0, 1, 1);

	Shader::AddGlobalShaderDefine("BLOCK_SIZE", std::to_string(FORWARD_PLUS_BLOCK_SIZE));
	Shader::AddGlobalShaderDefine("SHADOWMAP_DX", std::to_string(1.0f / SHADOW_MAP_SIZE));
	Shader::AddGlobalShaderDefine("PCF_KERNEL_SIZE", std::to_string(5));
	Shader::AddGlobalShaderDefine("MAX_SHADOW_CASTERS", std::to_string(MAX_SHADOW_CASTERS));

	InitD3D();
	InitializeAssets();

	m_pClouds = std::make_unique<Clouds>();
	m_pClouds->Initialize(this);

	RandomizeLights(m_DesiredLightCount);
}

void Graphics::Update()
{
	Profiler::Instance()->Begin("Update Game State");

	m_pCamera->Update();
	// render forward+ tiles
	if (Input::Instance().IsKeyPressed('P'))
	{
		m_UseDebugView = !m_UseDebugView;
	}

	if (Input::Instance().IsKeyPressed('O'))
	{
		RandomizeLights(m_DesiredLightCount);
	}

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

	Matrix projection = XMMatrixPerspectiveFovLH(Math::PIDIV2, 1.0f, m_Lights[0].Range, 0.1f);

	m_ShadowCasters = 0;

	// point light
	/*lightData.LightViewProjections[m_ShadowCasters] = Matrix::CreateLookAt(m_Lights[0].Position, m_Lights[0].Position + Vector3(-1.f, 0.f, 0.f), Vector3::Up) * projection;
	lightData.ShadowMapOffsets[m_ShadowCasters] = Vector4(0.f, 0.f, 0.25f, 0);
	++m_ShadowCasters;

	lightData.LightViewProjections[m_ShadowCasters] = Matrix::CreateLookAt(m_Lights[0].Position, m_Lights[0].Position + Vector3(1.f, 0.f, 0.f), Vector3::Up) * projection;
	lightData.ShadowMapOffsets[m_ShadowCasters] = Vector4(0.25f, 0.f, 0.25f, 0);
	++m_ShadowCasters;

	lightData.LightViewProjections[m_ShadowCasters] = Matrix::CreateLookAt(m_Lights[0].Position, m_Lights[0].Position + Vector3(0.f, -1.f, 0.f), Vector3::Forward) * projection;
	lightData.ShadowMapOffsets[m_ShadowCasters] = Vector4(0.5f, 0.f, 0.25f, 0);
	++m_ShadowCasters;

	lightData.LightViewProjections[m_ShadowCasters] = Matrix::CreateLookAt(m_Lights[0].Position, m_Lights[0].Position + Vector3(0.f, 1.f, 0.f), Vector3::Backward) * projection;
	lightData.ShadowMapOffsets[m_ShadowCasters] = Vector4(0.75f, 0.f, 0.25f, 0);
	++m_ShadowCasters;

	lightData.LightViewProjections[m_ShadowCasters] = Matrix::CreateLookAt(m_Lights[0].Position, m_Lights[0].Position + Vector3(0.f, 0.f, -1.f), Vector3::Up) * projection;
	lightData.ShadowMapOffsets[m_ShadowCasters] = Vector4(0.f, 0.25f, 0.25f, 0);
	++m_ShadowCasters;

	lightData.LightViewProjections[m_ShadowCasters] = Matrix::CreateLookAt(m_Lights[0].Position, m_Lights[0].Position + Vector3(0.f, 0.f, 1.f), Vector3::Up) * projection;
	lightData.ShadowMapOffsets[m_ShadowCasters] = Vector4(0.25f, 0.25f, 0.25f, 0);
	++m_ShadowCasters;*/

	//// main directional
	//lightData.LightViewProjections[m_ShadowCasters] = XMMatrixLookAtLH(m_Lights[m_ShadowCasters].Position, m_Lights[m_ShadowCasters].Position + m_Lights[m_ShadowCasters].Direction, Vector3::Up) * XMMatrixOrthographicLH(512, 512, 1000.f, 0.1f);
	//lightData.ShadowMapOffsets[m_ShadowCasters] = Vector4(0.f, 0.f, 0.75f, 0);

	//// spot A
	//++m_ShadowCasters;
	//lightData.LightViewProjections[m_ShadowCasters] = XMMatrixLookAtLH(m_Lights[m_ShadowCasters].Position, m_Lights[m_ShadowCasters].Position + m_Lights[m_ShadowCasters].Direction, Vector3::Up) * XMMatrixPerspectiveFovLH(XM_PIDIV2, 1.f, 300.f, 0.1f);
	//lightData.ShadowMapOffsets[m_ShadowCasters] = Vector4(0.75f, 0.f, 0.25f, 0);

	//// spot B
	//++m_ShadowCasters;
	//lightData.LightViewProjections[m_ShadowCasters] = XMMatrixLookAtLH(m_Lights[m_ShadowCasters].Position, m_Lights[m_ShadowCasters].Position + m_Lights[m_ShadowCasters].Direction, Vector3::Up) * XMMatrixPerspectiveFovLH(XM_PIDIV2, 1.f, 300.f, 0.1f);
	//lightData.ShadowMapOffsets[m_ShadowCasters] = Vector4(0.75f, 0.25f, 0.25f, 0);

	//// spot C
	//++m_ShadowCasters;
	//lightData.LightViewProjections[m_ShadowCasters] = XMMatrixLookAtLH(m_Lights[m_ShadowCasters].Position, m_Lights[m_ShadowCasters].Position + m_Lights[m_ShadowCasters].Direction, Vector3::Up) * XMMatrixPerspectiveFovLH(XM_PIDIV2, 1.f, 300.f, 0.1f);
	//lightData.ShadowMapOffsets[m_ShadowCasters] = Vector4(0.75f, 0.5f, 0.25f, 0);

	Profiler::Instance()->End();

	////////////////////////////////
	// Rendering Begin
	////////////////////////////////

	BeginFrame();

	uint64_t nextFenceValue = 0;
	uint64_t lightCullingFence = 0;

	if (m_RenderPath == RenderPath::Tiled)
	{
		Profiler::Instance()->Begin("Tiled Forward+");
		// 1. depth prepass
		// - depth only pass that renders the entire scene
		// - optimization that prevents wasteful lighting calculations during the base pass
		// - required for light culling
		{
			GraphicsCommandContext* pCommandContext = static_cast<GraphicsCommandContext*>(AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT));
			Profiler::Instance()->Begin("Depth Prepass", pCommandContext);

			pCommandContext->InsertResourceBarrier(GetDepthStencil(), D3D12_RESOURCE_STATE_DEPTH_WRITE);

			pCommandContext->BeginRenderPass(RenderPassInfo(GetDepthStencil(), RenderPassAccess::Clear_Store));
		
			pCommandContext->SetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			pCommandContext->SetViewport(FloatRect(0, 0, (float)m_WindowWidth, (float)m_WindowHeight));
			pCommandContext->SetScissorRect(FloatRect(0, 0, (float)m_WindowWidth, (float)m_WindowHeight));

			struct PerObjectData
			{
				Matrix WorldViewProjection;
			} ObjectData;

			pCommandContext->SetGraphicsPipelineState(m_pDepthPrepassPSO.get());
			pCommandContext->SetGraphicsRootSignature(m_pDepthPrepassRS.get());
			for (const Batch& b : m_OpaqueBatches)
			{
				ObjectData.WorldViewProjection = m_pCamera->GetViewProjection();
				pCommandContext->SetDynamicConstantBufferView(0, &ObjectData, sizeof(PerObjectData));
				b.pMesh->Draw(pCommandContext);
			}

			pCommandContext->EndRenderPass();

			pCommandContext->InsertResourceBarrier(GetDepthStencil(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, false);
			if (m_SampleCount > 1)
			{
				pCommandContext->InsertResourceBarrier(GetResolveDepthStencil(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
			}

			Profiler::Instance()->End(pCommandContext);
			uint64_t depthPrepassFence = pCommandContext->Execute(false);
			m_CommandQueues[D3D12_COMMAND_LIST_TYPE_COMPUTE]->InsertWaitForFence(depthPrepassFence);
		}

		// 2. [OPTIONAL] depth resolve
		//  - if MSAA is enabled, run a compute shader to resolve the depth buffer
		if (m_SampleCount > 1)
		{
			ComputeCommandContext* pCommandContext = static_cast<ComputeCommandContext*>(AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COMPUTE));
			Profiler::Instance()->Begin("Depth Resolve", pCommandContext);

			pCommandContext->SetComputeRootSignature(m_pResolveDepthRS.get());
			pCommandContext->SetComputePipelineState(m_pResolveDepthPSO.get());

			pCommandContext->SetDynamicDescriptor(0, 0, GetResolveDepthStencil()->GetUAV());
			pCommandContext->SetDynamicDescriptor(1, 0, GetDepthStencil()->GetSRV());

			int dispatchGroupX = Math::RoundUp(m_WindowWidth / 16.0f);
			int dispatchGroupY = Math::RoundUp(m_WindowHeight / 16.0f);
			pCommandContext->Dispatch(dispatchGroupX, dispatchGroupY, 1);

			pCommandContext->InsertResourceBarrier(GetResolveDepthStencil(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);
			Profiler::Instance()->End(pCommandContext);

			uint64_t resolveDepthFence = pCommandContext->Execute(false);
			m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT]->InsertWaitForFence(resolveDepthFence);
			m_CommandQueues[D3D12_COMMAND_LIST_TYPE_COMPUTE]->InsertWaitForFence(resolveDepthFence);
		}

		// 3. light culling
		//  - compute shader to buckets lights in tiles depending on their screen position
		//  - require a depth buffer
		//  - outputs a: - Texture2D containing a count and an offset of lights per tile.
		//								- uint[] index buffer to indicate what are visible in each tile
		{
			ComputeCommandContext* pCommandContext = static_cast<ComputeCommandContext*>(AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COMPUTE));
			Profiler::Instance()->Begin("Light Culling", pCommandContext);

			Profiler::Instance()->Begin("Setup Light Data", pCommandContext);
			uint32_t zero[] = { 0, 0 };
			m_pLightIndexCounter->SetData(pCommandContext, &zero, sizeof(uint32_t) * 2);
			m_pLightBuffer->SetData(pCommandContext, m_Lights.data(), sizeof(Light) * (uint32_t)m_Lights.size());
			Profiler::Instance()->End(pCommandContext);

			pCommandContext->InsertResourceBarrier(m_pLightGridOpaque.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			pCommandContext->InsertResourceBarrier(m_pLightIndexListBufferOpaque.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			pCommandContext->InsertResourceBarrier(m_pLightGridTransparent.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			pCommandContext->InsertResourceBarrier(m_pLightIndexListBufferTransparent.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			pCommandContext->SetComputePipelineState(m_pComputeLightCullPipeline.get());
			pCommandContext->SetComputeRootSignature(m_pComputeLightCullRS.get());

			struct ShaderParameter
			{
				Matrix CameraView;
				Matrix ProjectionInverse;
				uint32_t NumThreadGroups[4]{};
				Vector2 ScreenDimensions;
				uint32_t LightCount{0};
			} Data;

			Data.CameraView = m_pCamera->GetView();
			Data.NumThreadGroups[0] = Math::RoundUp((float)m_WindowWidth / FORWARD_PLUS_BLOCK_SIZE);
			Data.NumThreadGroups[1] = Math::RoundUp((float)m_WindowHeight / FORWARD_PLUS_BLOCK_SIZE);
			Data.NumThreadGroups[2] = 1;
			Data.ScreenDimensions.x = (float)m_WindowWidth;
			Data.ScreenDimensions.y = (float)m_WindowHeight;
			Data.LightCount = (uint32_t)m_Lights.size();
			Data.ProjectionInverse = m_pCamera->GetProjectionInverse();

			pCommandContext->SetComputeDynamicConstantBufferView(0, &Data, sizeof(ShaderParameter));
			pCommandContext->SetDynamicDescriptor(1, 0, m_pLightIndexCounter->GetUAV());
			pCommandContext->SetDynamicDescriptor(1, 1, m_pLightIndexListBufferOpaque->GetUAV());
			pCommandContext->SetDynamicDescriptor(1, 2, m_pLightGridOpaque->GetUAV());
			pCommandContext->SetDynamicDescriptor(1, 3, m_pLightIndexListBufferTransparent->GetUAV());
			pCommandContext->SetDynamicDescriptor(1, 4, m_pLightGridTransparent->GetUAV());
			pCommandContext->SetDynamicDescriptor(2, 0, GetResolveDepthStencil()->GetSRV());
			pCommandContext->SetDynamicDescriptor(2, 1, m_pLightBuffer->GetSRV());

			pCommandContext->Dispatch(Data.NumThreadGroups[0], Data.NumThreadGroups[1], Data.NumThreadGroups[2]);
			Profiler::Instance()->End(pCommandContext);

			lightCullingFence = pCommandContext->Execute(false);
		}

		// 4. shadow mapping
		//  - render shadow maps for directional and spot lights
		//  - renders the scene depth onto a separate depth buffer from the light's view
		if (m_ShadowCasters > 0)
		{
			GraphicsCommandContext* pCommandContext = static_cast<GraphicsCommandContext*>(AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT));
			Profiler::Instance()->Begin("Shadows", pCommandContext);

			pCommandContext->SetViewport(FloatRect(0, 0, (float)m_pShadowMap->GetWidth(), (float)m_pShadowMap->GetHeight()));
			pCommandContext->SetScissorRect(FloatRect(0, 0, (float)m_pShadowMap->GetWidth(), (float)m_pShadowMap->GetHeight()));

			pCommandContext->InsertResourceBarrier(m_pShadowMap.get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
		
			pCommandContext->BeginRenderPass(RenderPassInfo(GetDepthStencil(), RenderPassAccess::Clear_Store));

			pCommandContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			for (int i = 0; i < m_ShadowCasters; ++i)
			{
				Profiler::Instance()->Begin("Light View", pCommandContext);
				const Vector4& shadowOffset = lightData.ShadowMapOffsets[i];
				FloatRect viewport;
				viewport.Left = shadowOffset.x * (float)m_pShadowMap->GetWidth();
				viewport.Top = shadowOffset.y * (float)m_pShadowMap->GetHeight();
				viewport.Right = viewport.Left + shadowOffset.z * (float)m_pShadowMap->GetWidth();
				viewport.Bottom = viewport.Top + shadowOffset.z * (float)m_pShadowMap->GetHeight();
				pCommandContext->SetViewport(viewport);
				pCommandContext->SetScissorRect(viewport);

				struct PerObjectData
				{
					Matrix WorldViewProjection;
				} ObjectData;
				ObjectData.WorldViewProjection = lightData.LightViewProjections[i];

				// opaque
				{
					Profiler::Instance()->Begin("Opaque", pCommandContext);
					pCommandContext->SetGraphicsPipelineState(m_pShadowPSO.get());
					pCommandContext->SetGraphicsRootSignature(m_pShadowRS.get());

					pCommandContext->SetDynamicConstantBufferView(0, &ObjectData, sizeof(ObjectData));
					for (const Batch& b : m_OpaqueBatches)
					{
						b.pMesh->Draw(pCommandContext);
					}
					Profiler::Instance()->End(pCommandContext);
				}

				// transparent
				{
					Profiler::Instance()->Begin("Transparent", pCommandContext);
					pCommandContext->SetGraphicsPipelineState(m_pShadowAlphaPSO.get());
					pCommandContext->SetGraphicsRootSignature(m_pShadowAlphaRS.get());

					pCommandContext->SetDynamicConstantBufferView(0, &ObjectData, sizeof(ObjectData));
					for (const Batch& b : m_TransparentBatches)
					{
						pCommandContext->SetDynamicDescriptor(1, 0, b.pMaterial->pDiffuseTexture->GetSRV());
						b.pMesh->Draw(pCommandContext);
					}
					Profiler::Instance()->End(pCommandContext);
				}
				Profiler::Instance()->End(pCommandContext);
			}
			pCommandContext->EndRenderPass();

			Profiler::Instance()->End(pCommandContext);
			pCommandContext->Execute(false);
		}

		// cant do the lighting until the light culling is complete
		m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT]->InsertWaitForFence(lightCullingFence);

		// 5. base pass
		//  - render the scene using the shadow mapping result and the light culling buffers
		{
			GraphicsCommandContext* pCommandContext = static_cast<GraphicsCommandContext*>(AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT));
			Profiler::Instance()->Begin("Base Pass", pCommandContext);

			pCommandContext->SetViewport(FloatRect(0, 0, (float)m_WindowWidth, (float)m_WindowHeight));
			pCommandContext->SetScissorRect(FloatRect(0, 0, (float)m_WindowWidth, (float)m_WindowHeight));
	
			pCommandContext->InsertResourceBarrier(m_pShadowMap.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			pCommandContext->InsertResourceBarrier(m_pLightGridOpaque.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			pCommandContext->InsertResourceBarrier(m_pLightIndexListBufferOpaque.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			pCommandContext->InsertResourceBarrier(m_pLightGridTransparent.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			pCommandContext->InsertResourceBarrier(m_pLightIndexListBufferTransparent.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			pCommandContext->InsertResourceBarrier(GetDepthStencil(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
			pCommandContext->InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET);

			pCommandContext->BeginRenderPass(RenderPassInfo(GetCurrentRenderTarget(), RenderPassAccess::Clear_Store, GetDepthStencil(), RenderPassAccess::Load_DontCare));

			pCommandContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	
			struct PerObjectData
			{
				Matrix World;
				Matrix WorldViewProjection;
			} objectData;

			// opaque
			{
				Profiler::Instance()->Begin("Opaque", pCommandContext);
				pCommandContext->SetGraphicsPipelineState(m_UseDebugView ? m_pDiffusePSODebug.get() : m_pDiffusePSO.get());
				pCommandContext->SetGraphicsRootSignature(m_pDiffuseRS.get());

				pCommandContext->SetDynamicConstantBufferView(1, &frameData, sizeof(PerFrameData));
				pCommandContext->SetDynamicConstantBufferView(2, &lightData, sizeof(LightData));
				pCommandContext->SetDynamicDescriptor(4, 0, m_pShadowMap->GetSRV());
				pCommandContext->SetDynamicDescriptor(4, 1, m_pLightGridOpaque->GetSRV());
				pCommandContext->SetDynamicDescriptor(4, 2, m_pLightIndexListBufferOpaque->GetSRV());
				pCommandContext->SetDynamicDescriptor(4, 3, m_pLightBuffer->GetSRV());
				
				for (const Batch& b : m_OpaqueBatches)
				{
					objectData.World = XMMatrixIdentity();
					objectData.WorldViewProjection = objectData.World * m_pCamera->GetViewProjection();
					pCommandContext->SetDynamicConstantBufferView(0, &objectData, sizeof(PerObjectData));

					pCommandContext->SetDynamicDescriptor(3, 0, b.pMaterial->pDiffuseTexture->GetSRV());
					pCommandContext->SetDynamicDescriptor(3, 1, b.pMaterial->pNormalTexture->GetSRV());
					pCommandContext->SetDynamicDescriptor(3, 2, b.pMaterial->pSpecularTexture->GetSRV());

					b.pMesh->Draw(pCommandContext);
				}
				Profiler::Instance()->End(pCommandContext);
			}

			// transparent
			{
				Profiler::Instance()->Begin("Transparent", pCommandContext);
				pCommandContext->SetGraphicsPipelineState(m_UseDebugView ? m_pDiffusePSODebug.get() : m_pDiffuseAlphaPSO.get());
				pCommandContext->SetGraphicsRootSignature(m_pDiffuseRS.get());

				pCommandContext->SetDynamicConstantBufferView(1, &frameData, sizeof(PerFrameData));
				pCommandContext->SetDynamicConstantBufferView(2, &lightData, sizeof(LightData));
				pCommandContext->SetDynamicDescriptor(4, 0, m_pShadowMap->GetSRV());
				pCommandContext->SetDynamicDescriptor(4, 1, m_pLightGridTransparent->GetSRV());
				pCommandContext->SetDynamicDescriptor(4, 2, m_pLightIndexListBufferTransparent->GetSRV());
				pCommandContext->SetDynamicDescriptor(4, 3, m_pLightBuffer->GetSRV());

				for (const Batch& b : m_TransparentBatches)
				{
					objectData.World = XMMatrixIdentity();
					objectData.WorldViewProjection = objectData.World * m_pCamera->GetViewProjection();
					pCommandContext->SetDynamicConstantBufferView(0, &objectData, sizeof(PerObjectData));

					pCommandContext->SetDynamicDescriptor(3, 0, b.pMaterial->pDiffuseTexture->GetSRV());
					pCommandContext->SetDynamicDescriptor(3, 1, b.pMaterial->pNormalTexture->GetSRV());
					pCommandContext->SetDynamicDescriptor(3, 2, b.pMaterial->pSpecularTexture->GetSRV());

					b.pMesh->Draw(pCommandContext);
				}
				Profiler::Instance()->End(pCommandContext);
			}

			Profiler::Instance()->End(pCommandContext);

			pCommandContext->InsertResourceBarrier(m_pLightGridOpaque.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			pCommandContext->InsertResourceBarrier(m_pLightIndexListBufferOpaque.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			pCommandContext->InsertResourceBarrier(m_pLightGridTransparent.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			pCommandContext->InsertResourceBarrier(m_pLightIndexListBufferTransparent.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			pCommandContext->EndRenderPass();
			pCommandContext->Execute(false);
		}
		Profiler::Instance()->End();
	}
	else if (m_RenderPath == RenderPath::Clustered)
	{
		Profiler::Instance()->Begin("Clustered Forward+");
		ClusteredForwardInputResource resources;
		resources.pOpaqueBatches = &m_OpaqueBatches;
		resources.pTransparentBatches = &m_TransparentBatches;
		resources.pRenderTarget = GetCurrentRenderTarget();
		resources.pLightBuffer = m_pLightBuffer.get();
		resources.pCamera = m_pCamera.get();
		m_pClusteredForward->Execute(resources);
		Profiler::Instance()->End();
	}

	{
		GraphicsCommandContext* pCommandContext = static_cast<GraphicsCommandContext*>(AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT));
		Profiler::Instance()->Begin("UI", pCommandContext);
		// 6. UI
		//  - ImGui render, pretty straight forward
		{
			UpdateImGui();
			//ImGui::ShowDemoWindow();
			m_pClouds->RenderUI();
			m_pImGuiRenderer->Render(*pCommandContext);
		}
		Profiler::Instance()->End(pCommandContext);

		// 7. MSAA render target resolve
		//  - we have to resolve a MSAA render target ourselves.
		{
			if (m_SampleCount > 1)
			{
				Profiler::Instance()->Begin("Resolve MSAA", pCommandContext);
				
				pCommandContext->InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
				pCommandContext->InsertResourceBarrier(m_pResolvedRenderTarget.get(), D3D12_RESOURCE_STATE_RESOLVE_DEST);
				pCommandContext->FlushResourceBarriers();
				pCommandContext->GetCommandList()->ResolveSubresource(m_pResolvedRenderTarget->GetResource(), 0, GetCurrentRenderTarget()->GetResource(), 0, RENDER_TARGET_FORMAT);

				Profiler::Instance()->End(pCommandContext);
			}

			pCommandContext->InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET);
			pCommandContext->InsertResourceBarrier(GetCurrentBackbuffer(), D3D12_RESOURCE_STATE_PRESENT);
		}

		pCommandContext->Execute(false);
	}

	m_pClouds->Render(this, m_pResolvedRenderTarget.get(), m_pResolveDepthStencil.get());

	{
		GraphicsCommandContext* pCommandContext = static_cast<GraphicsCommandContext*>(AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT));
		Profiler::Instance()->Begin("Blit to Backbuffer", pCommandContext);
		pCommandContext->InsertResourceBarrier(m_pResolvedRenderTarget.get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
		pCommandContext->InsertResourceBarrier(GetCurrentBackbuffer(), D3D12_RESOURCE_STATE_COPY_DEST);
		pCommandContext->FlushResourceBarriers();
		pCommandContext->CopyResource(m_pResolvedRenderTarget.get(), GetCurrentBackbuffer());
		pCommandContext->InsertResourceBarrier(GetCurrentBackbuffer(), D3D12_RESOURCE_STATE_PRESENT);
		Profiler::Instance()->End(pCommandContext);

		nextFenceValue = pCommandContext->Execute(false);
	}

	// 8. present
	//  - set fence for the currently queued frame
	//  - present the frame buffer
	//  - wait for the next frame to be finished to start queueing work for it
	EndFrame(nextFenceValue);
}

void Graphics::Shutdown()
{
	// wait for all the GPU work to finish
	IdleGPU();
}

void Graphics::BeginFrame()
{
	if (m_StartPixCapture)
	{
		BeginPixCapture();
		m_StartPixCapture = false;
		m_EndPixCapture = true;
	}
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

	if (m_EndPixCapture)
	{
		EndPixCapture();
		m_EndPixCapture = false;
	}
}

void Graphics::InitD3D()
{
	E_LOG(LogType::Info, "Graphics::InitD3D");
	UINT dxgiFactoryFlags = 0;

#ifdef _DEBUG
	// enable debug
	ComPtr<ID3D12Debug> pDebugController;
	HR(D3D12GetDebugInterface(IID_PPV_ARGS(&pDebugController)));
	pDebugController->EnableDebugLayer();

	// additional debug layers
	dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

	// factory
	HR(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_pFactory)));

	// look for an adapter
	uint32_t adapter = 0;
	IDXGIAdapter1* pAdapter;
	while (m_pFactory->EnumAdapterByGpuPreference(adapter, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&pAdapter)) != DXGI_ERROR_NOT_FOUND)
	{
		DXGI_ADAPTER_DESC1 desc;
		pAdapter->GetDesc1(&desc);

		if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
		{
			wprintf(L"Selected adapter: %s\n", desc.Description);
			break;
		}

		pAdapter->Release();
		pAdapter = nullptr;
		++adapter;
	}

	// device
	HR(D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_pDevice)));

#ifdef _DEBUG
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
	for (size_t i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; i++)
	{
		m_DescriptorHeaps[i] = std::make_unique<DescriptorAllocator>(m_pDevice.Get(), (D3D12_DESCRIPTOR_HEAP_TYPE)i);
	}

	m_pDynamicAllocationManager = std::make_unique<DynamicAllocationManager>(this);
	m_pPersistentAllocationManager = std::make_unique<PersistentResourceAllocator>(GetDevice());
	Profiler::Instance()->Initialize(this);

	// swap chain
	CreateSwapchain();

	// create the textures but don't create the resources themselves yet. 
	for (int i = 0; i < FRAME_COUNT; i++)
	{
		m_RenderTargets[i] = std::make_unique<GraphicsTexture2D>();
	}
	m_pDepthStencil = std::make_unique<GraphicsTexture2D>();

	if (m_SampleCount > 1)
	{
		m_pResolveDepthStencil = std::make_unique<GraphicsTexture2D>();
		m_pResolvedRenderTarget = std::make_unique<GraphicsTexture2D>();
		m_pMultiSampleRenderTarget = std::make_unique<GraphicsTexture2D>();
	}

	m_pLightGridOpaque = std::make_unique<GraphicsTexture2D>();
	m_pLightGridTransparent = std::make_unique<GraphicsTexture2D>();

	m_pClusteredForward = std::make_unique<ClusteredForward>(this);

	OnResize(m_WindowWidth, m_WindowHeight);

	m_pImGuiRenderer = std::make_unique<ImGuiRenderer>(this);
}

void Graphics::CreateSwapchain()
{
	m_pSwapchain.Reset();

	DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
	swapchainDesc.Width = m_WindowWidth;
	swapchainDesc.Height = m_WindowHeight;
	swapchainDesc.Format = RENDER_TARGET_FORMAT;
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
	E_LOG(LogType::Info, "Graphics::OnResize()");
	m_WindowWidth = width;
	m_WindowHeight = height;

	IdleGPU();
	
	for (int i = 0; i < FRAME_COUNT; i++)
	{
		m_RenderTargets[i]->Release();
	}
	m_pDepthStencil->Release();

	// resize the buffers
	HR(m_pSwapchain->ResizeBuffers(
			FRAME_COUNT,
			m_WindowWidth,
			m_WindowHeight,
			RENDER_TARGET_FORMAT,
			DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

	m_CurrentBackBufferIndex = 0;

	// recreate the render target views
	for (int i = 0; i < FRAME_COUNT; i++)
	{
		ID3D12Resource* pResource = nullptr;
 		HR(m_pSwapchain->GetBuffer(i, IID_PPV_ARGS(&pResource)));
		m_RenderTargets[i]->CreateForSwapChain(this, pResource);
		m_RenderTargets[i]->SetName("RenderTarget");
	}

	if (m_SampleCount > 1)
	{
		m_pDepthStencil->Create(this, m_WindowWidth, m_WindowHeight, DEPTH_STENCIL_FORMAT, TextureUsage::DepthStencil | TextureUsage::ShaderResource, m_SampleCount, -1, ClearBinding(0.0f, 0));
		m_pDepthStencil->SetName("Depth Stencil");
		m_pResolveDepthStencil->Create(this, m_WindowWidth, m_WindowHeight, DXGI_FORMAT_R32_FLOAT, TextureUsage::ShaderResource | TextureUsage::UnorderedAccess, 1, -1, ClearBinding(0.0f, 0));
		m_pResolveDepthStencil->SetName("Resolve Depth Stencil");

		m_pMultiSampleRenderTarget->Create(this, width, height, RENDER_TARGET_FORMAT, TextureUsage::RenderTarget, m_SampleCount, -1, ClearBinding(Color(0,0,0,0)));
		m_pMultiSampleRenderTarget->SetName("Multisample Rendertarget");

		m_pResolvedRenderTarget->Create(this, width, height, RENDER_TARGET_FORMAT, TextureUsage::RenderTarget | TextureUsage::ShaderResource, 1, -1, ClearBinding(Color(0,0,0,0)));
		m_pResolvedRenderTarget->SetName("Resolved Render Target");
	}
	else
	{
		m_pDepthStencil->Create(this, m_WindowWidth, m_WindowHeight, DEPTH_STENCIL_FORMAT, TextureUsage::DepthStencil | TextureUsage::ShaderResource, m_SampleCount, -1, ClearBinding(0.0f, 0));
		m_pDepthStencil->SetName("Depth Stencil");
	}

	int frustumCountX = (int)ceil((float)m_WindowWidth / FORWARD_PLUS_BLOCK_SIZE);
	int frustumCountY = (int)ceil((float)m_WindowHeight / FORWARD_PLUS_BLOCK_SIZE);
	m_pLightGridOpaque->Create(this, frustumCountX, frustumCountY, DXGI_FORMAT_R32G32_UINT, TextureUsage::UnorderedAccess | TextureUsage::ShaderResource, 1);
	m_pLightGridTransparent->Create(this, frustumCountX, frustumCountY, DXGI_FORMAT_R32G32_UINT, TextureUsage::UnorderedAccess | TextureUsage::ShaderResource, 1);

	m_pClusteredForward->OnSwapchainCreated(width, height);
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
	};
	
	D3D12_INPUT_ELEMENT_DESC depthOnlyAlphaInputElements[] = {
			D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	// diffuse passes
	{
		// shaders
		Shader vertexShader("Resources/Shaders/Diffuse.hlsl", Shader::Type::VertexShader, "VSMain", { /*"SHADOW"*/});
		Shader pixelShader("Resources/Shaders/Diffuse.hlsl", Shader::Type::PixelShader, "PSMain", { /*"SHADOW"*/ });

		// root signature
		m_pDiffuseRS = std::make_unique<RootSignature>();
		m_pDiffuseRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
		m_pDiffuseRS->SetConstantBufferView(1, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pDiffuseRS->SetConstantBufferView(2, 2, D3D12_SHADER_VISIBILITY_PIXEL);
		m_pDiffuseRS->SetDescriptorTableSimple(3, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, D3D12_SHADER_VISIBILITY_PIXEL);
		m_pDiffuseRS->SetDescriptorTableSimple(4, 3, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, D3D12_SHADER_VISIBILITY_PIXEL);

		// static samplers
		D3D12_SAMPLER_DESC samplerDesc{};
		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
		m_pDiffuseRS->AddStaticSampler(0, samplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);

		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		samplerDesc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
		m_pDiffuseRS->AddStaticSampler(1, samplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);

		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
		samplerDesc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
		m_pDiffuseRS->AddStaticSampler(2, samplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);

		D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
		m_pDiffuseRS->Finalize("Diffuse RS", m_pDevice.Get(), rootSignatureFlags);

		// opaque
		{
			m_pDiffusePSO = std::make_unique<GraphicsPipelineState>();
			m_pDiffusePSO->SetInputLayout(inputElements, sizeof(inputElements) / sizeof(inputElements[0]));
			m_pDiffusePSO->SetRootSignature(m_pDiffuseRS->GetRootSignature());
			m_pDiffusePSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
			m_pDiffusePSO->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
			m_pDiffusePSO->SetRenderTargetFormat(RENDER_TARGET_FORMAT, DEPTH_STENCIL_FORMAT, m_SampleCount, m_SampleQuality);
			m_pDiffusePSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
			m_pDiffusePSO->SetDepthWrite(false);
			m_pDiffusePSO->Finalize("Diffuse (Opaque) Pipeline", m_pDevice.Get());
		}

		// transparent
		{
			m_pDiffuseAlphaPSO = std::make_unique<GraphicsPipelineState>();
			m_pDiffuseAlphaPSO->SetInputLayout(inputElements, sizeof(inputElements) / sizeof(inputElements[0]));
			m_pDiffuseAlphaPSO->SetRootSignature(m_pDiffuseRS->GetRootSignature());
			m_pDiffuseAlphaPSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
			m_pDiffuseAlphaPSO->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
			m_pDiffuseAlphaPSO->SetRenderTargetFormat(RENDER_TARGET_FORMAT, DEPTH_STENCIL_FORMAT, m_SampleCount, m_SampleQuality);
			m_pDiffuseAlphaPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
			m_pDiffuseAlphaPSO->SetCullMode(D3D12_CULL_MODE_NONE);
			m_pDiffuseAlphaPSO->SetDepthWrite(false);
			m_pDiffuseAlphaPSO->SetBlendMode(BlendMode::Alpha, false);
			m_pDiffuseAlphaPSO->Finalize("Diffuse (Alpha) Pipeline", m_pDevice.Get());
		}

		// debug version
		{
			pixelShader = Shader("Resources/Shaders/Diffuse.hlsl", Shader::Type::PixelShader, "PSMain", { "DEBUG_VISUALIZE" });

			m_pDiffusePSODebug = std::make_unique<GraphicsPipelineState>(*m_pDiffusePSO.get());
			m_pDiffusePSODebug->SetInputLayout(inputElements, sizeof(inputElements) / sizeof(inputElements[0]));
			m_pDiffusePSODebug->SetRootSignature(m_pDiffuseRS->GetRootSignature());
			m_pDiffusePSODebug->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
			m_pDiffusePSODebug->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
			m_pDiffusePSODebug->SetRenderTargetFormat(RENDER_TARGET_FORMAT, DEPTH_STENCIL_FORMAT, m_SampleCount, m_SampleQuality);
			m_pDiffusePSODebug->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
			m_pDiffusePSODebug->SetDepthWrite(false);
			m_pDiffusePSODebug->Finalize("Diffuse (Debug) Pipeline", m_pDevice.Get());
		}
	}

	// shadow mapping
	// vertex shader-only pass that writes to the depth buffer using the light matrix
	{
		// opaque
		{
			Shader vertexShader("Resources/Shaders/DepthOnly.hlsl", Shader::Type::VertexShader, "VSMain");

			// root signature
			m_pShadowRS = std::make_unique<RootSignature>();
			m_pShadowRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

			D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
				D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
			m_pShadowRS->Finalize("Shadow Mapping RS", m_pDevice.Get(), rootSignatureFlags);

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
		}

		// transparent
		{
			Shader vertexShader("Resources/Shaders/DepthOnly.hlsl", Shader::Type::VertexShader, "VSMain", { "ALPHA_BLEND" });
			Shader pixelShader("Resources/Shaders/DepthOnly.hlsl", Shader::Type::PixelShader, "PSMain", { "ALPHA_BLEND" });

			// root signature
			m_pShadowAlphaRS = std::make_unique<RootSignature>();
			m_pShadowAlphaRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
			m_pShadowAlphaRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, D3D12_SHADER_VISIBILITY_PIXEL);

			D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
				D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

			D3D12_SAMPLER_DESC samplerDesc{};
			samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
			samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
			samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
			m_pShadowAlphaRS->AddStaticSampler(0, samplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);

			m_pShadowAlphaRS->Finalize("Shadow Mapping Alpha RS", m_pDevice.Get(), rootSignatureFlags);

			// pipeline state
			m_pShadowAlphaPSO = std::make_unique<GraphicsPipelineState>();
			m_pShadowAlphaPSO->SetInputLayout(depthOnlyAlphaInputElements, sizeof(depthOnlyAlphaInputElements) / sizeof(depthOnlyAlphaInputElements[0]));
			m_pShadowAlphaPSO->SetRootSignature(m_pShadowAlphaRS->GetRootSignature());
			m_pShadowAlphaPSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
			m_pShadowAlphaPSO->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
			m_pShadowAlphaPSO->SetRenderTargetFormats(nullptr, 0, DEPTH_STENCIL_SHADOW_FORMAT, 1, 0);
			m_pShadowAlphaPSO->SetCullMode(D3D12_CULL_MODE_NONE);
			m_pShadowAlphaPSO->SetDepthBias(0, 0.f, 0.f);
			m_pShadowAlphaPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
			m_pShadowAlphaPSO->Finalize("Shadow Mapping (Alpha) Pipeline", m_pDevice.Get());
		}

		m_pShadowMap = std::make_unique<GraphicsTexture2D>();
		m_pShadowMap->Create(this, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, DEPTH_STENCIL_SHADOW_FORMAT, TextureUsage::DepthStencil | TextureUsage::ShaderResource, 1, -1, ClearBinding(0.0f, 0));
	}

	// depth prepass
	// simple vertex shader to fill the depth buffer to optimize later passes
	{
		Shader vertexShader("Resources/Shaders/DepthOnly.hlsl", Shader::Type::VertexShader, "VSMain");

		// root signature
		m_pDepthPrepassRS = std::make_unique<RootSignature>();
		m_pDepthPrepassRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

		D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
		m_pDepthPrepassRS->Finalize("Depth Prepass RS", m_pDevice.Get(), rootSignatureFlags);

		// pipeline state
		m_pDepthPrepassPSO = std::make_unique<GraphicsPipelineState>();
		m_pDepthPrepassPSO->SetInputLayout(depthOnlyInputElements, sizeof(depthOnlyInputElements) / sizeof(depthOnlyInputElements[0]));
		m_pDepthPrepassPSO->SetRootSignature(m_pDepthPrepassRS->GetRootSignature());
		m_pDepthPrepassPSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pDepthPrepassPSO->SetRenderTargetFormats(nullptr, 0, DEPTH_STENCIL_FORMAT, m_SampleCount, m_SampleQuality);
		m_pDepthPrepassPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		m_pDepthPrepassPSO->Finalize("Depth Prepass Pipeline", m_pDevice.Get());
	}

	// depth resolve
	// resolves a multisampled buffer to a normal depth buffer
	// only required when the sample count > 1
	if (m_SampleCount > 1)
	{
		Shader computeShader("Resources/Shaders/ResolveDepth.hlsl", Shader::Type::ComputeShader, "CSMain");

		m_pResolveDepthRS = std::make_unique<RootSignature>();
		m_pResolveDepthRS->SetDescriptorTableSimple(0, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pResolveDepthRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pResolveDepthRS->Finalize("Resolve Depth RS", m_pDevice.Get(), D3D12_ROOT_SIGNATURE_FLAG_NONE);

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
		m_pComputeLightCullRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
		m_pComputeLightCullRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 5, D3D12_SHADER_VISIBILITY_ALL);
		m_pComputeLightCullRS->SetDescriptorTableSimple(2, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, D3D12_SHADER_VISIBILITY_ALL);
		m_pComputeLightCullRS->Finalize("Compute Light Culling RS", m_pDevice.Get(), D3D12_ROOT_SIGNATURE_FLAG_NONE);

		m_pComputeLightCullPipeline = std::make_unique<ComputePipelineState>();
		m_pComputeLightCullPipeline->SetRootSignature(m_pComputeLightCullRS->GetRootSignature());
		m_pComputeLightCullPipeline->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pComputeLightCullPipeline->Finalize("Compute Light Culling Pipeline", m_pDevice.Get());

		m_pLightIndexCounter = std::make_unique<StructuredBuffer>(this);
		m_pLightIndexCounter->Create(this, sizeof(uint32_t), 2);
		m_pLightIndexListBufferOpaque = std::make_unique<StructuredBuffer>(this);
		m_pLightIndexListBufferOpaque->Create(this, sizeof(uint32_t), MAX_LIGHT_DENSITY);
		m_pLightIndexListBufferTransparent = std::make_unique<StructuredBuffer>(this);
		m_pLightIndexListBufferTransparent->Create(this, sizeof(uint32_t), MAX_LIGHT_DENSITY);
		m_pLightBuffer = std::make_unique<StructuredBuffer>(this);
	}

	// geometry
	{
		CopyCommandContext* pCommandContext = static_cast<CopyCommandContext*>(AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COPY));
		
		m_pMesh = std::make_unique<Mesh>();
		m_pMesh->Load("Resources/sponza/sponza.dae", this, pCommandContext);

		pCommandContext->Execute(true);

		for (int i = 0; i < m_pMesh->GetMeshCount(); i++)
		{
			Batch b;
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
	ImGui::SetNextWindowSize(ImVec2(250, (float)m_WindowHeight));
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
		
		extern bool gUseAlternativeLightCulling;
		ImGui::Checkbox("Alternative Light Culling", &gUseAlternativeLightCulling);

		extern bool gVisualizeClusters;
		ImGui::Checkbox("Visualize Clusters", &gVisualizeClusters);

		ImGui::Separator();
		ImGui::SliderInt("Lights", &m_DesiredLightCount, 0, 16384);
		if (ImGui::Button("Generate Lights"))
		{
			RandomizeLights(m_DesiredLightCount);
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

			uint32_t totalDescriptors = pAllocator->GetHeapCount() * DescriptorAllocator::DESCRIPTORS_PER_HEAP;
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
	ImGui::SetNextWindowPos(ImVec2(250, showOutputLog ? (float)m_WindowHeight - 250 : (float)m_WindowHeight - 20));
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
				break;
			case LogType::Warning:
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 0, 1));
				break;
			case LogType::Error:
			case LogType::FatalError:
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0, 0, 1));
				break;
			}
			ImGui::TextWrapped("[Error] %s", entry.Message.c_str());
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
	m_Lights[lightIndex] = Light::Point(Vector3(0, 20, 0), 200);
	m_Lights[lightIndex].ShadowIndex = lightIndex;

	int randomLightsStartIndex = lightIndex + 1;
	for (int i = randomLightsStartIndex; i < m_Lights.size(); i++)
	{
		Vector4 color(Math::RandomRange(0.f, 1.f), Math::RandomRange(0.f, 1.f), Math::RandomRange(0.f, 1.f), 1);

		Vector3 position;
		position.x = Math::RandomRange(-sceneBounds.Extents.x, sceneBounds.Extents.x) + sceneBounds.Center.x;
		position.y = Math::RandomRange(-sceneBounds.Extents.y, sceneBounds.Extents.y) + sceneBounds.Center.y;
		position.z = Math::RandomRange(-sceneBounds.Extents.z, sceneBounds.Extents.z) + sceneBounds.Center.z;

		const float range = Math::RandomRange(7.f, 12.f);
		const float angle = Math::RandomRange(30.f, 60.f);

		Light::Type type = (rand() % 2 == 0) ? Light::Type::Point : Light::Type::Spot;
		switch (type)
		{
		case Light::Type::Point:
			m_Lights[i] = Light::Point(position, range, 1.0f, 0.5f, color);
			break;
		case Light::Type::Spot:
			m_Lights[i] = Light::Spot(position, range, Math::RandVector(), angle, 1.0f, 0.5f, color);
			break;
		case Light::Type::Directional:
		case Light::Type::MAX:
		default:
			break;
		}
	}

	std::sort(m_Lights.begin() + randomLightsStartIndex, m_Lights.end(), [](const Light& a, const Light b) { return (int)a.LightType < (int)b.LightType; });
	
	IdleGPU();
	if (m_pLightBuffer->GetElementCount() != count)
	{
		m_pLightBuffer->Create(this, sizeof(Light), count);
		m_pLightBuffer->SetName("Light Buffer");
	}
	GraphicsCommandContext* pContext = static_cast<GraphicsCommandContext*>(AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT));
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

		switch (type)
		{
		case D3D12_COMMAND_LIST_TYPE_DIRECT:
			m_CommandListPool[typeIndex].emplace_back(std::make_unique<GraphicsCommandContext>(this, static_cast<ID3D12GraphicsCommandList*>(m_CommandLists.back().Get()), pAllocator));
			m_CommandListPool[typeIndex].back()->SetName("Pooled Graphics Command Context");
			break;
		case D3D12_COMMAND_LIST_TYPE_COMPUTE:
			m_CommandListPool[typeIndex].emplace_back(std::make_unique<ComputeCommandContext>(this, static_cast<ID3D12GraphicsCommandList*>(m_CommandLists.back().Get()), pAllocator));
			m_CommandListPool[typeIndex].back()->SetName("Pooled Compute Command Context");
			break;
		case D3D12_COMMAND_LIST_TYPE_COPY:
			m_CommandListPool[typeIndex].emplace_back(std::make_unique<CopyCommandContext>(this, static_cast<ID3D12GraphicsCommandList*>(m_CommandLists.back().Get()), pAllocator));
			m_CommandListPool[typeIndex].back()->SetName("Pooled Copy Command Context");
			break;
		default:
			assert(false);
			break;
		}

		return m_CommandListPool[typeIndex].back().get();
	}
}

D3D12_CPU_DESCRIPTOR_HANDLE Graphics::AllocateCpuDescriptors(int count, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
	assert((int)type < m_DescriptorHeaps.size());
	return m_DescriptorHeaps[type]->AllocateDescriptors(count);
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
	/*if (heapType == D3D12_HEAP_TYPE_DEFAULT)
	{
		pResource = m_pPersistentAllocationManager->CreateResource(desc, initialState, pClearValue);
	}
	else*/
	{
		D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(heapType);
		HR(m_pDevice->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			initialState,
			pClearValue,
			IID_PPV_ARGS(&pResource)));
	}

	return pResource;
}

