#include "stdafx.h"
#include "Graphics.h"
#include "CommandAllocatorPool.h"
#include "CommandQueue.h"
#include "CommandContext.h"
#include "DescriptorAllocator.h"
#include "DynamicResourceAllocator.h"
#include "ImGuiRenderer.h"
#include "External/imgui/imgui.h"
#include "GraphicsResource.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "Shader.h"
#include "Mesh.h"
#include "Input.h"

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

	Shader::AddGlobalShaderDefine("LIGHT_COUNT", std::to_string(MAX_LIGHT_COUNT));
	Shader::AddGlobalShaderDefine("BLOCK_SIZE", std::to_string(FORWARD_PLUS_BLOCK_SIZE));
	Shader::AddGlobalShaderDefine("SHADOWMAP_DX", std::to_string(1.0f / SHADOW_MAP_SIZE));
	Shader::AddGlobalShaderDefine("PCF_KERNEL_SIZE", std::to_string(3));
	Shader::AddGlobalShaderDefine("MAX_SHADOW_CASTERS", std::to_string(MAX_SHADOW_CASTERS));

	InitD3D();
	InitializeAssets();

	m_FrameTimes.resize(64*3);

	m_CameraPosition = Vector3(0, 100, -15);
	m_CameraRotation = Quaternion::CreateFromYawPitchRoll(XM_PIDIV4, XM_PIDIV4, 0);

	RandomizeLights();
}

void Graphics::Update()
{
	// render forward+ tiles
	if (Input::Instance().IsKeyPressed('P'))
	{
		m_UseDebugView = !m_UseDebugView;
	}

	if (Input::Instance().IsKeyPressed('O'))
	{
		RandomizeLights();
	}

	// camera movement
	if (Input::Instance().IsMouseDown(VK_LBUTTON))
	{
		Vector2 mouseDelta = Input::Instance().GetMouseDelta();
		Quaternion yr = Quaternion::CreateFromYawPitchRoll(mouseDelta.x * GameTimer::DeltaTime() * 0.1f, 0, 0);
		Quaternion pr = Quaternion::CreateFromYawPitchRoll(0, mouseDelta.y * GameTimer::DeltaTime() * 0.1f, 0);
		m_CameraRotation = pr * m_CameraRotation * yr;
	}

	Vector3 movement;
	movement.x -= (int)Input::Instance().IsKeyDown('A');
	movement.x += (int)Input::Instance().IsKeyDown('D');
	movement.z -= (int)Input::Instance().IsKeyDown('S');
	movement.z += (int)Input::Instance().IsKeyDown('W');
	movement = Vector3::Transform(movement, m_CameraRotation);
	movement.y -= (int)Input::Instance().IsKeyDown('Q');
	movement.y += (int)Input::Instance().IsKeyDown('E');
	movement *= GameTimer::DeltaTime() * 20.0f;
	m_CameraPosition += movement;

	// set main light position
	m_Lights[0].Position = Vector3(cos(GameTimer::GameTime() / 5.0f), 1.5f, sin(GameTimer::GameTime() / 5.0f)) * 120;
	m_Lights[0].Position.Normalize(m_Lights[0].Direction);
	m_Lights[0].Direction *= -1;

	SortBatchesBackToFront(m_CameraPosition, m_TransparentBatches);

	// per frame constants
	////////////////////////////////////
	struct PerFrameData
	{
		Matrix ViewInverse;
	} frameData;

	// camera constants
	frameData.ViewInverse = Matrix::CreateFromQuaternion(m_CameraRotation) * Matrix::CreateTranslation(m_CameraPosition);

	Matrix cameraView;
	frameData.ViewInverse.Invert(cameraView);
	Matrix cameraProj = XMMatrixPerspectiveFovLH(XM_PIDIV4, (float)m_WindowWidth / m_WindowHeight, 100000.0f, 0.1f);
	Matrix cameraViewProj = cameraView * cameraProj;

	// shadow map partitioning
	//////////////////////////////////
	struct LightData
	{
		Matrix LightViewProjections[MAX_SHADOW_CASTERS];
		Vector4 ShadowMapOffsets[MAX_SHADOW_CASTERS];
	} lightData;

	// main directional
	m_ShadowCasters = 0;
	lightData.LightViewProjections[m_ShadowCasters] = XMMatrixLookAtLH(m_Lights[m_ShadowCasters].Position, m_Lights[m_ShadowCasters].Position + m_Lights[m_ShadowCasters].Direction, Vector3::Up) * XMMatrixOrthographicLH(512, 512, 1000.f, 0.1f);
	lightData.ShadowMapOffsets[m_ShadowCasters] = Vector4(0.f, 0.f, 0.75f, 0);

	// spot A
	++m_ShadowCasters;
	lightData.LightViewProjections[m_ShadowCasters] = XMMatrixLookAtLH(m_Lights[m_ShadowCasters].Position, m_Lights[m_ShadowCasters].Position + m_Lights[m_ShadowCasters].Direction, Vector3::Up) * XMMatrixPerspectiveFovLH(XM_PIDIV2, 1.f, 300.f, 0.1f);
	lightData.ShadowMapOffsets[m_ShadowCasters] = Vector4(0.75f, 0.f, 0.25f, 0);

	// spot B
	++m_ShadowCasters;
	lightData.LightViewProjections[m_ShadowCasters] = XMMatrixLookAtLH(m_Lights[m_ShadowCasters].Position, m_Lights[m_ShadowCasters].Position + m_Lights[m_ShadowCasters].Direction, Vector3::Up) * XMMatrixPerspectiveFovLH(XM_PIDIV2, 1.f, 300.f, 0.1f);
	lightData.ShadowMapOffsets[m_ShadowCasters] = Vector4(0.75f, 0.25f, 0.25f, 0);

	// spot C
	++m_ShadowCasters;
	lightData.LightViewProjections[m_ShadowCasters] = XMMatrixLookAtLH(m_Lights[m_ShadowCasters].Position, m_Lights[m_ShadowCasters].Position + m_Lights[m_ShadowCasters].Direction, Vector3::Up) * XMMatrixPerspectiveFovLH(XM_PIDIV2, 1.f, 300.f, 0.1f);
	lightData.ShadowMapOffsets[m_ShadowCasters] = Vector4(0.75f, 0.5f, 0.25f, 0);

	////////////////////////////////
	// Rendering Begin
	////////////////////////////////

	BeginFrame();

	uint64_t nextFenceValue = 0;
	uint64_t lightCullingFence = 0;

	// 1. depth prepass
	// - depth only pass that renders the entire scene
	// - optimization that prevents wasteful lighting calculations during the base pass
	// - required for light culling
	{
		GraphicsCommandContext* pCommandContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT)->AsGraphicsContext();
		pCommandContext->MarkBegin(L"Depth Prepass");

		pCommandContext->InsertResourceBarrier(GetDepthStencil(), D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
		pCommandContext->SetDepthOnlyTarget(GetDepthStencil()->GetDSV());
		pCommandContext->ClearDepth(GetDepthStencil()->GetDSV(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 0.0f, 0);
		
		pCommandContext->SetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pCommandContext->SetViewport(FloatRect(0, 0, (float)m_WindowWidth, (float)m_WindowHeight));
		pCommandContext->SetScissorRect(FloatRect(0, 0, (float)m_WindowWidth, (float)m_WindowHeight));

		struct PerObjectData
		{
			Matrix WorldViewProjection;
		} ObjectData;
		ObjectData.WorldViewProjection = cameraViewProj;

		pCommandContext->SetPipelineState(m_pDepthPrepassPipelineStateObject.get());
		pCommandContext->SetRootSignature(m_pDepthPrepassRootSignature.get());
		pCommandContext->SetDynamicConstantBufferView(0, &ObjectData, sizeof(PerObjectData));
		for (const Batch& b : m_OpaqueBatches)
		{
			b.pMesh->Draw(pCommandContext);
		}

		pCommandContext->InsertResourceBarrier(GetDepthStencil(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, false);
		if (m_SampleCount > 1)
		{
			pCommandContext->InsertResourceBarrier(GetResolveDepthStencil(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
		}

		pCommandContext->MarkEnd();
		uint64_t depthPrepassFence = pCommandContext->Execute(false);
		m_CommandQueues[D3D12_COMMAND_LIST_TYPE_COMPUTE]->InsertWaitForFence(depthPrepassFence);
	}

	// 2. [OPTIONAL] depth resolve
	//  - if MSAA is enabled, run a compute shader to resolve the depth buffer
	if (m_SampleCount > 1)
	{
		ComputeCommandContext* pCommandContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COMPUTE)->AsComputeContext();
		pCommandContext->MarkBegin(L"Depth Resolve");

		pCommandContext->SetRootSignature(m_pResolveDepthRootSignature.get());
		pCommandContext->SetPipelineState(m_pResolveDepthPipelineStateObject.get());

		pCommandContext->SetDynamicDescriptor(0, 0, GetResolveDepthStencil()->GetUAV());
		pCommandContext->SetDynamicDescriptor(1, 0, GetDepthStencil()->GetSRV());

		int dispatchGroupX = Math::RoundUp(m_WindowWidth / 16.0f);
		int dispatchGroupY = Math::RoundUp(m_WindowHeight / 16.0f);
		pCommandContext->Dispatch(dispatchGroupX, dispatchGroupY, 1);

		pCommandContext->InsertResourceBarrier(GetResolveDepthStencil(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);
		pCommandContext->MarkEnd();

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
		ComputeCommandContext* pCommandContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COMPUTE)->AsComputeContext();
		pCommandContext->MarkBegin(L"Light Culling");

		pCommandContext->MarkBegin(L"Setup Light Data");
		uint32_t zero[] = { 0, 0 };
		m_pLightIndexCounter->SetData(pCommandContext, &zero, sizeof(uint32_t) * 2);
		m_pLightBuffer->SetData(pCommandContext, m_Lights.data(), sizeof(Light) * (uint32_t)m_Lights.size());
		pCommandContext->MarkEnd();

		pCommandContext->SetPipelineState(m_pComputeLightCullPipeline.get());
		pCommandContext->SetRootSignature(m_pComputeLightCullRootSignature.get());

		struct ShaderParameter
		{
			Matrix CameraView;
			uint32_t NumThreadGroups[4]{};
			Matrix ProjectionInverse;
			Vector2 ScreenDimensions;
		} Data;

		Data.CameraView = cameraView;
		Data.NumThreadGroups[0] = Math::RoundUp((float)m_WindowWidth / FORWARD_PLUS_BLOCK_SIZE);
		Data.NumThreadGroups[1] = Math::RoundUp((float)m_WindowHeight / FORWARD_PLUS_BLOCK_SIZE);
		Data.NumThreadGroups[2] = 1;
		Data.ScreenDimensions.x = (float)m_WindowWidth;
		Data.ScreenDimensions.y = (float)m_WindowHeight;
		cameraProj.Invert(Data.ProjectionInverse);

		pCommandContext->SetDynamicConstantBufferView(0, &Data, sizeof(ShaderParameter));
		pCommandContext->SetDynamicDescriptor(1, 0, m_pLightIndexCounter->GetUAV());
		pCommandContext->SetDynamicDescriptor(1, 1, m_pLightIndexListBufferOpaque->GetUAV());
		pCommandContext->SetDynamicDescriptor(1, 2, m_pLightGridOpaque->GetUAV());
		pCommandContext->SetDynamicDescriptor(1, 3, m_pLightIndexListBufferTransparent->GetUAV());
		pCommandContext->SetDynamicDescriptor(1, 4, m_pLightGridTransparent->GetUAV());
		pCommandContext->SetDynamicDescriptor(2, 0, GetResolveDepthStencil()->GetSRV());
		pCommandContext->SetDynamicDescriptor(2, 1, m_pLightBuffer->GetSRV());

		pCommandContext->Dispatch(Data.NumThreadGroups[0], Data.NumThreadGroups[1], Data.NumThreadGroups[2]);
		pCommandContext->MarkEnd();

		lightCullingFence = pCommandContext->Execute(false);
	}

	// 4. shadow mapping
	//  - render shadow maps for directional and spot lights
	//  - renders the scene depth onto a separate depth buffer from the light's view
	if (m_ShadowCasters > 0)
	{
		GraphicsCommandContext* pCommandContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT)->AsGraphicsContext();
		pCommandContext->MarkBegin(L"Shadows");

		pCommandContext->SetViewport(FloatRect(0, 0, (float)m_pShadowMap->GetWidth(), (float)m_pShadowMap->GetHeight()));
		pCommandContext->SetScissorRect(FloatRect(0, 0, (float)m_pShadowMap->GetWidth(), (float)m_pShadowMap->GetHeight()));

		pCommandContext->InsertResourceBarrier(m_pShadowMap.get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
		pCommandContext->SetDepthOnlyTarget(m_pShadowMap->GetDSV());

		pCommandContext->ClearDepth(m_pShadowMap->GetDSV(), D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0);

		pCommandContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		for (int i = 0; i < m_ShadowCasters; ++i)
		{
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
				pCommandContext->MarkBegin(L"Opaque");
				pCommandContext->SetPipelineState(m_pShadowPipelineStateObject.get());
				pCommandContext->SetRootSignature(m_pShadowRootSignature.get());

				pCommandContext->SetDynamicConstantBufferView(0, &ObjectData, sizeof(ObjectData));
				for (const Batch& b : m_OpaqueBatches)
				{
					b.pMesh->Draw(pCommandContext);
				}
				pCommandContext->MarkEnd();
			}

			// transparent
			{
				pCommandContext->MarkBegin(L"Transparent");
				pCommandContext->SetPipelineState(m_pShadowAlphaPipelineStateObject.get());
				pCommandContext->SetRootSignature(m_pShadowAlphaRootSignature.get());

				pCommandContext->SetDynamicConstantBufferView(0, &ObjectData, sizeof(ObjectData));
				for (const Batch& b : m_TransparentBatches)
				{
					pCommandContext->SetDynamicDescriptor(1, 0, b.pMaterial->pDiffuseTexture->GetSRV());
					b.pMesh->Draw(pCommandContext);
				}
				pCommandContext->MarkEnd();
			}
			
		}

		pCommandContext->MarkEnd();
		pCommandContext->Execute(false);
	}

	// cant do the lighting until the light culling is complete
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT]->InsertWaitForFence(lightCullingFence);

	// 5. base pass
	//  - render the scene using the shadow mapping result and the light culling buffers
	{
		GraphicsCommandContext* pCommandContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT)->AsGraphicsContext();
		pCommandContext->MarkBegin(L"Base Pass");

		pCommandContext->SetViewport(FloatRect(0, 0, (float)m_WindowWidth, (float)m_WindowHeight));
		pCommandContext->SetScissorRect(FloatRect(0, 0, (float)m_WindowWidth, (float)m_WindowHeight));
	
		pCommandContext->InsertResourceBarrier(m_pShadowMap.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, false);
		pCommandContext->InsertResourceBarrier(m_pLightGridOpaque.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, false);
		pCommandContext->InsertResourceBarrier(m_pLightIndexListBufferOpaque.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, false);
		pCommandContext->InsertResourceBarrier(m_pLightGridTransparent.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, false);
		pCommandContext->InsertResourceBarrier(m_pLightIndexListBufferTransparent.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, false);
		pCommandContext->InsertResourceBarrier(GetDepthStencil(), D3D12_RESOURCE_STATE_DEPTH_WRITE, false);
		pCommandContext->InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET, true);
		
		pCommandContext->ClearRenderTarget(GetCurrentRenderTarget()->GetRTV());
		pCommandContext->SetRenderTarget(GetCurrentRenderTarget()->GetRTV(), GetDepthStencil()->GetDSV());

		pCommandContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	
		struct PerObjectData
		{
			Matrix World;
			Matrix WorldViewProjection;
		} objectData;

		objectData.World = XMMatrixIdentity();
		objectData.WorldViewProjection = objectData.World * cameraViewProj;

		// opaque
		{
			pCommandContext->MarkBegin(L"Opaque");
			pCommandContext->SetPipelineState(m_UseDebugView ? m_pDiffusePipelineStateObjectDebug.get() : m_pDiffusePipelineStateObject.get());
			pCommandContext->SetRootSignature(m_pDiffuseRootSignature.get());

			pCommandContext->SetDynamicConstantBufferView(0, &objectData, sizeof(PerObjectData));
			pCommandContext->SetDynamicConstantBufferView(1, &frameData, sizeof(PerFrameData));
			pCommandContext->SetDynamicConstantBufferView(2, &lightData, sizeof(LightData));
			pCommandContext->SetDynamicDescriptor(4, 0, m_pShadowMap->GetSRV());
			pCommandContext->SetDynamicDescriptor(4, 1, m_pLightGridOpaque->GetSRV());
			pCommandContext->SetDynamicDescriptor(4, 2, m_pLightIndexListBufferOpaque->GetSRV());
			pCommandContext->SetDynamicDescriptor(4, 3, m_pLightBuffer->GetSRV());

			for (const Batch& b : m_OpaqueBatches)
			{
				pCommandContext->SetDynamicDescriptor(3, 0, b.pMaterial->pDiffuseTexture->GetSRV());
				pCommandContext->SetDynamicDescriptor(3, 1, b.pMaterial->pNormalTexture->GetSRV());
				pCommandContext->SetDynamicDescriptor(3, 2, b.pMaterial->pSpecularTexture->GetSRV());

				b.pMesh->Draw(pCommandContext);
			}
			pCommandContext->MarkEnd();
		}

		// transparent
		{
			pCommandContext->MarkBegin(L"Transparent");
			pCommandContext->SetPipelineState(m_UseDebugView ? m_pDiffusePipelineStateObjectDebug.get() : m_pDiffuseAlphaPipelineStateObject.get());
			pCommandContext->SetRootSignature(m_pDiffuseRootSignature.get());

			pCommandContext->SetDynamicConstantBufferView(0, &objectData, sizeof(PerObjectData));
			pCommandContext->SetDynamicConstantBufferView(1, &frameData, sizeof(PerFrameData));
			pCommandContext->SetDynamicConstantBufferView(2, &lightData, sizeof(LightData));
			pCommandContext->SetDynamicDescriptor(4, 0, m_pShadowMap->GetSRV());
			pCommandContext->SetDynamicDescriptor(4, 1, m_pLightGridTransparent->GetSRV());
			pCommandContext->SetDynamicDescriptor(4, 2, m_pLightIndexListBufferTransparent->GetSRV());
			pCommandContext->SetDynamicDescriptor(4, 3, m_pLightBuffer->GetSRV());

			for (const Batch& b : m_TransparentBatches)
			{
				pCommandContext->SetDynamicDescriptor(3, 0, b.pMaterial->pDiffuseTexture->GetSRV());
				pCommandContext->SetDynamicDescriptor(3, 1, b.pMaterial->pNormalTexture->GetSRV());
				pCommandContext->SetDynamicDescriptor(3, 2, b.pMaterial->pSpecularTexture->GetSRV());

				b.pMesh->Draw(pCommandContext);
			}
			pCommandContext->MarkEnd();
		}

		pCommandContext->MarkEnd();

		pCommandContext->InsertResourceBarrier(m_pLightGridOpaque.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, false);
		pCommandContext->InsertResourceBarrier(m_pLightIndexListBufferOpaque.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
		pCommandContext->InsertResourceBarrier(m_pLightGridTransparent.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, false);
		pCommandContext->InsertResourceBarrier(m_pLightIndexListBufferTransparent.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);

		pCommandContext->Execute(false);
	}

	{
		GraphicsCommandContext* pCommandContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT)->AsGraphicsContext();
		pCommandContext->MarkBegin(L"UI");

		// 6. UI
		//  - ImGui render, pretty straight forward
		{
			UpdateImGui();
			m_pImGuiRenderer->Render(*pCommandContext);
		}
		pCommandContext->MarkEnd();

		pCommandContext->MarkBegin(L"Present");
		// 7. MSAA render target resolve
		//  - we have to resolve a MSAA render target ourselves.
		{
			if (m_SampleCount > 1)
			{
				pCommandContext->InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE, false);
				pCommandContext->InsertResourceBarrier(GetCurrentBackbuffer(), D3D12_RESOURCE_STATE_RESOLVE_DEST, true);
				pCommandContext->GetCommandList()->ResolveSubresource(GetCurrentBackbuffer()->GetResource(), 0, GetCurrentRenderTarget()->GetResource(), 0, RENDER_TARGET_FORMAT);
			}
			pCommandContext->InsertResourceBarrier(GetCurrentBackbuffer(), D3D12_RESOURCE_STATE_PRESENT, true);

			pCommandContext->MarkEnd();
			nextFenceValue = pCommandContext->Execute(false);
		}
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
	m_pImGuiRenderer->NewFrame();
}

void Graphics::EndFrame(uint64_t fenceValue)
{
	// this also gets me confused
	// the 'm_CurrentBackBufferIndex' is the frame that just got queued so we set the fence value on that frame
	// we present and request the new backbuffer index and wait for that one to finish on the GPU before starting to queue work for that frame

	m_FenceValues[m_CurrentBackBufferIndex] = fenceValue;
	m_pSwapchain->Present(1, 0);
	m_CurrentBackBufferIndex = m_pSwapchain->GetCurrentBackBufferIndex();
	WaitForFence(m_FenceValues[m_CurrentBackBufferIndex]);
	m_pDynamicCpuVisibleAllocator->ResetAllocationCounter();
}

void Graphics::InitD3D()
{
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
	HR(D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_pDevice)));

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

	m_pDynamicCpuVisibleAllocator = std::make_unique<DynamicResourceAllocator>(this, true, 0x400000);

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
		for (int i = 0; i < FRAME_COUNT; i++)
		{
			m_MultiSampleRenderTargets[i] = std::make_unique<GraphicsTexture2D>();
		}
	}

	m_pLightGridOpaque = std::make_unique<GraphicsTexture2D>();
	m_pLightGridTransparent = std::make_unique<GraphicsTexture2D>();

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

		if (m_SampleCount > 1)
		{
			m_MultiSampleRenderTargets[i]->Create(this, m_WindowWidth, m_WindowHeight, RENDER_TARGET_FORMAT, TextureUsage::RenderTarget, m_SampleCount);
		}
	}

	if (m_SampleCount > 1)
	{
		m_pDepthStencil->Create(this, m_WindowWidth, m_WindowHeight, DEPTH_STENCIL_FORMAT, TextureUsage::DepthStencil | TextureUsage::ShaderResource, m_SampleCount);
		m_pResolveDepthStencil->Create(this, m_WindowWidth, m_WindowHeight, DXGI_FORMAT_R32_FLOAT, TextureUsage::ShaderResource | TextureUsage::UnorderedAccess, 1);
	}
	else
	{
		m_pDepthStencil->Create(this, m_WindowWidth, m_WindowHeight, DEPTH_STENCIL_FORMAT, TextureUsage::DepthStencil | TextureUsage::ShaderResource, m_SampleCount);
	}

	int frustumCountX = (int)ceil((float)m_WindowWidth / FORWARD_PLUS_BLOCK_SIZE);
	int frustumCountY = (int)ceil((float)m_WindowHeight / FORWARD_PLUS_BLOCK_SIZE);
	m_pLightGridOpaque->Create(this, frustumCountX, frustumCountY, DXGI_FORMAT_R32G32_UINT, TextureUsage::UnorderedAccess | TextureUsage::ShaderResource, 1);
	m_pLightGridTransparent->Create(this, frustumCountX, frustumCountY, DXGI_FORMAT_R32G32_UINT, TextureUsage::UnorderedAccess | TextureUsage::ShaderResource, 1);
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
		Shader vertexShader("Resources/Diffuse.hlsl", Shader::Type::VertexShader, "VSMain");
		Shader pixelShader("Resources/Diffuse.hlsl", Shader::Type::PixelShader, "PSMain");

		// root signature
		m_pDiffuseRootSignature = std::make_unique<RootSignature>(5);
		m_pDiffuseRootSignature->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
		m_pDiffuseRootSignature->SetConstantBufferView(1, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pDiffuseRootSignature->SetConstantBufferView(2, 2, D3D12_SHADER_VISIBILITY_PIXEL);
		m_pDiffuseRootSignature->SetDescriptorTableSimple(3, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, D3D12_SHADER_VISIBILITY_PIXEL);
		m_pDiffuseRootSignature->SetDescriptorTableSimple(4, 3, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, D3D12_SHADER_VISIBILITY_PIXEL);

		// static samplers
		D3D12_SAMPLER_DESC samplerDesc{};
		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
		m_pDiffuseRootSignature->AddStaticSampler(0, samplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);

		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		samplerDesc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
		m_pDiffuseRootSignature->AddStaticSampler(1, samplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);

		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
		samplerDesc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
		m_pDiffuseRootSignature->AddStaticSampler(2, samplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);

		D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
		m_pDiffuseRootSignature->Finalize(m_pDevice.Get(), rootSignatureFlags);

		// opaque
		{
			m_pDiffusePipelineStateObject = std::make_unique<GraphicsPipelineState>();
			m_pDiffusePipelineStateObject->SetInputLayout(inputElements, sizeof(inputElements) / sizeof(inputElements[0]));
			m_pDiffusePipelineStateObject->SetRootSignature(m_pDiffuseRootSignature->GetRootSignature());
			m_pDiffusePipelineStateObject->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
			m_pDiffusePipelineStateObject->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
			m_pDiffusePipelineStateObject->SetRenderTargetFormat(RENDER_TARGET_FORMAT, DEPTH_STENCIL_FORMAT, m_SampleCount, m_SampleQuality);
			m_pDiffusePipelineStateObject->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
			m_pDiffusePipelineStateObject->SetDepthWrite(false);
			m_pDiffusePipelineStateObject->Finalize(m_pDevice.Get());
		}

		// transparent
		{
			m_pDiffuseAlphaPipelineStateObject = std::make_unique<GraphicsPipelineState>();
			m_pDiffuseAlphaPipelineStateObject->SetInputLayout(inputElements, sizeof(inputElements) / sizeof(inputElements[0]));
			m_pDiffuseAlphaPipelineStateObject->SetRootSignature(m_pDiffuseRootSignature->GetRootSignature());
			m_pDiffuseAlphaPipelineStateObject->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
			m_pDiffuseAlphaPipelineStateObject->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
			m_pDiffuseAlphaPipelineStateObject->SetRenderTargetFormat(RENDER_TARGET_FORMAT, DEPTH_STENCIL_FORMAT, m_SampleCount, m_SampleQuality);
			m_pDiffuseAlphaPipelineStateObject->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
			m_pDiffuseAlphaPipelineStateObject->SetDepthWrite(false);
			m_pDiffuseAlphaPipelineStateObject->SetBlendMode(BlendMode::ALPHA, false);
			m_pDiffuseAlphaPipelineStateObject->Finalize(m_pDevice.Get());
		}

		// debug version
		{
			pixelShader = Shader("Resources/Diffuse.hlsl", Shader::Type::PixelShader, "PSMain", { "DEBUG_VISUALIZE" });

			m_pDiffusePipelineStateObjectDebug = std::make_unique<GraphicsPipelineState>(*m_pDiffusePipelineStateObject.get());
			m_pDiffusePipelineStateObjectDebug->SetInputLayout(inputElements, sizeof(inputElements) / sizeof(inputElements[0]));
			m_pDiffusePipelineStateObjectDebug->SetRootSignature(m_pDiffuseRootSignature->GetRootSignature());
			m_pDiffusePipelineStateObjectDebug->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
			m_pDiffusePipelineStateObjectDebug->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
			m_pDiffusePipelineStateObjectDebug->SetRenderTargetFormat(RENDER_TARGET_FORMAT, DEPTH_STENCIL_FORMAT, m_SampleCount, m_SampleQuality);
			m_pDiffusePipelineStateObjectDebug->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
			m_pDiffusePipelineStateObjectDebug->SetDepthWrite(false);
			m_pDiffusePipelineStateObjectDebug->Finalize(m_pDevice.Get());
		}
	}

	// shadow mapping
	// vertex shader-only pass that writes to the depth buffer using the light matrix
	{
		// opaque
		{
			Shader vertexShader("Resources/DepthOnly.hlsl", Shader::Type::VertexShader, "VSMain");

			// root signature
			m_pShadowRootSignature = std::make_unique<RootSignature>(1);
			m_pShadowRootSignature->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

			D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
				D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
			m_pShadowRootSignature->Finalize(m_pDevice.Get(), rootSignatureFlags);

			// pipeline state
			m_pShadowPipelineStateObject = std::make_unique<GraphicsPipelineState>();
			m_pShadowPipelineStateObject->SetInputLayout(depthOnlyInputElements, sizeof(depthOnlyInputElements) / sizeof(depthOnlyInputElements[0]));
			m_pShadowPipelineStateObject->SetRootSignature(m_pShadowRootSignature->GetRootSignature());
			m_pShadowPipelineStateObject->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
			m_pShadowPipelineStateObject->SetRenderTargetFormats(nullptr, 0, DEPTH_STENCIL_SHADOW_FORMAT, 1, 0);
			m_pShadowPipelineStateObject->SetCullMode(D3D12_CULL_MODE_NONE);
			m_pShadowPipelineStateObject->SetDepthBias(0, 0.f, -4.f);
			m_pShadowPipelineStateObject->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
			m_pShadowPipelineStateObject->Finalize(m_pDevice.Get());
		}

		// transparent
		{
			Shader vertexShader("Resources/DepthOnly.hlsl", Shader::Type::VertexShader, "VSMain", { "ALPHA_BLEND" });
			Shader pixelShader("Resources/DepthOnly.hlsl", Shader::Type::PixelShader, "PSMain", { "ALPHA_BLEND" });

			// root signature
			m_pShadowAlphaRootSignature = std::make_unique<RootSignature>(2);
			m_pShadowAlphaRootSignature->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
			m_pShadowAlphaRootSignature->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, D3D12_SHADER_VISIBILITY_PIXEL);

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
			m_pShadowAlphaRootSignature->AddStaticSampler(0, samplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);

			m_pShadowAlphaRootSignature->Finalize(m_pDevice.Get(), rootSignatureFlags);

			// pipeline state
			m_pShadowAlphaPipelineStateObject = std::make_unique<GraphicsPipelineState>();
			m_pShadowAlphaPipelineStateObject->SetInputLayout(depthOnlyAlphaInputElements, sizeof(depthOnlyAlphaInputElements) / sizeof(depthOnlyAlphaInputElements[0]));
			m_pShadowAlphaPipelineStateObject->SetRootSignature(m_pShadowAlphaRootSignature->GetRootSignature());
			m_pShadowAlphaPipelineStateObject->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
			m_pShadowAlphaPipelineStateObject->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
			m_pShadowAlphaPipelineStateObject->SetRenderTargetFormats(nullptr, 0, DEPTH_STENCIL_SHADOW_FORMAT, 1, 0);
			m_pShadowAlphaPipelineStateObject->SetCullMode(D3D12_CULL_MODE_NONE);
			m_pShadowAlphaPipelineStateObject->SetDepthBias(0, 0.f, 0.f);
			m_pShadowAlphaPipelineStateObject->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
			m_pShadowAlphaPipelineStateObject->Finalize(m_pDevice.Get());
		}

		m_pShadowMap = std::make_unique<GraphicsTexture2D>();
		m_pShadowMap->Create(this, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, DEPTH_STENCIL_SHADOW_FORMAT, TextureUsage::DepthStencil | TextureUsage::ShaderResource, 1);
	}

	// depth prepass
	// simple vertex shader to fill the depth buffer to optimize later passes
	{
		Shader vertexShader("Resources/DepthOnly.hlsl", Shader::Type::VertexShader, "VSMain");

		// root signature
		m_pDepthPrepassRootSignature = std::make_unique<RootSignature>(1);
		m_pDepthPrepassRootSignature->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

		D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
		m_pDepthPrepassRootSignature->Finalize(m_pDevice.Get(), rootSignatureFlags);

		// pipeline state
		m_pDepthPrepassPipelineStateObject = std::make_unique<GraphicsPipelineState>();
		m_pDepthPrepassPipelineStateObject->SetInputLayout(depthOnlyInputElements, sizeof(depthOnlyInputElements) / sizeof(depthOnlyInputElements[0]));
		m_pDepthPrepassPipelineStateObject->SetRootSignature(m_pDepthPrepassRootSignature->GetRootSignature());
		m_pDepthPrepassPipelineStateObject->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pDepthPrepassPipelineStateObject->SetRenderTargetFormats(nullptr, 0, DEPTH_STENCIL_FORMAT, m_SampleCount, m_SampleQuality);
		m_pDepthPrepassPipelineStateObject->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		m_pDepthPrepassPipelineStateObject->Finalize(m_pDevice.Get());
	}

	// depth resolve
	// resolves a multisampled buffer to a normal depth buffer
	// only required when the sample count > 1
	if (m_SampleCount > 1)
	{
		Shader computeShader("Resources/ResolveDepth.hlsl", Shader::Type::ComputeShader, "CSMain");

		m_pResolveDepthRootSignature = std::make_unique<RootSignature>(2);
		m_pResolveDepthRootSignature->SetDescriptorTableSimple(0, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pResolveDepthRootSignature->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pResolveDepthRootSignature->Finalize(m_pDevice.Get(), D3D12_ROOT_SIGNATURE_FLAG_NONE);

		m_pResolveDepthPipelineStateObject = std::make_unique<ComputePipelineState>();
		m_pResolveDepthPipelineStateObject->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pResolveDepthPipelineStateObject->SetRootSignature(m_pResolveDepthRootSignature->GetRootSignature());
		m_pResolveDepthPipelineStateObject->Finalize(m_pDevice.Get());
	}

	// light culling
	// compute shader that required depth buffer and light data to place lights into tiles
	{
		Shader computeShader("Resources/LightCulling.hlsl", Shader::Type::ComputeShader, "CSMain");

		m_pComputeLightCullRootSignature = std::make_unique<RootSignature>(3);
		m_pComputeLightCullRootSignature->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
		m_pComputeLightCullRootSignature->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 5, D3D12_SHADER_VISIBILITY_ALL);
		m_pComputeLightCullRootSignature->SetDescriptorTableSimple(2, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, D3D12_SHADER_VISIBILITY_ALL);
		m_pComputeLightCullRootSignature->Finalize(m_pDevice.Get(), D3D12_ROOT_SIGNATURE_FLAG_NONE);

		m_pComputeLightCullPipeline = std::make_unique<ComputePipelineState>();
		m_pComputeLightCullPipeline->SetRootSignature(m_pComputeLightCullRootSignature->GetRootSignature());
		m_pComputeLightCullPipeline->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pComputeLightCullPipeline->Finalize(m_pDevice.Get());

		m_pLightIndexCounter = std::make_unique<StructuredBuffer>();
		m_pLightIndexCounter->Create(this, sizeof(uint32_t), 2);
		m_pLightIndexListBufferOpaque = std::make_unique<StructuredBuffer>();
		m_pLightIndexListBufferOpaque->Create(this, sizeof(uint32_t), MAX_LIGHT_DENSITY);
		m_pLightIndexListBufferTransparent = std::make_unique<StructuredBuffer>();
		m_pLightIndexListBufferTransparent->Create(this, sizeof(uint32_t), MAX_LIGHT_DENSITY);
		m_pLightBuffer = std::make_unique<StructuredBuffer>();
		m_pLightBuffer->Create(this, sizeof(Light), MAX_LIGHT_COUNT, false);
	}

	// geometry
	{
		CopyCommandContext* pCommandContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT)->AsCopyContext();
		
		GameTimer::CounterBegin();
		m_pMesh = std::make_unique<Mesh>();
		m_pMesh->Load("Resources/sponza/sponza.dae", this, pCommandContext);
		m_LoadSponzaTime = GameTimer::CounterEnd();

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
	for (int i = 1; i < m_FrameTimes.size(); i++)
	{
		m_FrameTimes[i - 1] = m_FrameTimes[i];
	}
	m_FrameTimes[m_FrameTimes.size() - 1] = GameTimer::DeltaTime();
	
	ImGui::SetNextWindowPos(ImVec2(0, 0), 0, ImVec2(0, 0));
	ImGui::SetNextWindowSize(ImVec2(250, (float)m_WindowHeight));
	ImGui::Begin("GPU Stats", nullptr,
		ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	ImGui::Text("MS: %.4f", GameTimer::DeltaTime() * 1000.0f);
	ImGui::SameLine(100);
	ImGui::Text("FPS: %.1f", 1.0f / GameTimer::DeltaTime());

	ImGui::PlotLines("Frametime", m_FrameTimes.data(), (int)m_FrameTimes.size(), 0, 0, 0.0f, 0.03f, ImVec2(200, 100));

	ImGui::Text("LoadSponzaTime: %.1f", m_LoadSponzaTime);
	ImGui::Text("Light Count: %d", MAX_LIGHT_COUNT);

	ImGui::BeginTabBar("GpuStatBar");
	if (ImGui::BeginTabItem("Descriptor Heaps"))
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

		ImGui::EndTabItem();
	}

	if (ImGui::BeginTabItem("Memory"))
	{
		ImGui::Text("Used Dynamic Memory: %d KB", m_pDynamicCpuVisibleAllocator->GetTotalMemoryAllocation() / 1024);
		ImGui::Text("Peak Dynamic Memory: %d KB", m_pDynamicCpuVisibleAllocator->GetTotalMemoryAllocationPeak() / 1024);
		ImGui::EndTabItem();
	}

	ImGui::EndTabBar();
	ImGui::End();
}

void Graphics::RandomizeLights()
{
	BoundingBox sceneBounds;
	sceneBounds.Center = Vector3(0, 70, 0);
	sceneBounds.Extents = Vector3(140, 70, 60);

	m_Lights.resize(MAX_LIGHT_COUNT);

	int lightIndex = 0;
	Vector3 mainLightPosition = Vector3(cos((float)GameTimer::GameTime() / 5.0f), 1.5, sin((float)GameTimer::GameTime() / 5.0f)) * 120;
	Vector3 mainLightDirection;
	mainLightPosition.Normalize(mainLightDirection);
	mainLightDirection *= -1;
	m_Lights[lightIndex] = Light::Directional(mainLightPosition, mainLightDirection);
	m_Lights[lightIndex].ShadowIndex = lightIndex;
	++lightIndex;
	m_Lights[lightIndex] = Light::Spot(Vector3(0, 20, 0), 200, Vector3::Right, 40.0f, 1.0f, 0.5f, Vector4(1, 1, 1, 1));
	m_Lights[lightIndex].ShadowIndex = lightIndex;
	++lightIndex;
	m_Lights[lightIndex] = Light::Spot(Vector3(0, 20, 0), 200, Vector3::Forward, 40.0f, 1.0f, 0.5f, Vector4(1, 1, 1, 1));
	m_Lights[lightIndex].ShadowIndex = lightIndex;
	++lightIndex;
	m_Lights[lightIndex] = Light::Spot(Vector3(0, 20, 0), 200, Vector3::Backward, 40.0f, 1.0f, 0.5f, Vector4(1, 1, 1, 1));
	m_Lights[lightIndex].ShadowIndex = lightIndex;

	int randomLightsStartIndex = lightIndex + 1;
	for (int i = randomLightsStartIndex; i < m_Lights.size(); i++)
	{
		Vector4 color(Math::RandomRange(0.f, 1.f), Math::RandomRange(0.f, 1.f), Math::RandomRange(0.f, 1.f), 1);

		Vector3 position;
		position.x = Math::RandomRange(-sceneBounds.Extents.x, sceneBounds.Extents.x) + sceneBounds.Center.x;
		position.y = Math::RandomRange(-sceneBounds.Extents.y, sceneBounds.Extents.y) + sceneBounds.Center.y;
		position.z = Math::RandomRange(-sceneBounds.Extents.z, sceneBounds.Extents.z) + sceneBounds.Center.z;

		const float range = Math::RandomRange(15.f, 25.f);
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

	// a bit weird
	std::sort(m_Lights.begin() + randomLightsStartIndex, m_Lights.end(), [](const Light& a, const Light b) { return (int)a.LightType < (int)b.LightType; });
}

void Graphics::SortBatchesBackToFront(const Vector3& cameraPosition, std::vector<Batch>& batches)
{
	std::sort(batches.begin(), batches.end(), [cameraPosition](const Batch& a, const Batch& b) {
		float aDist = Vector3::DistanceSquared(a.WorldMatrix.Translation(), cameraPosition);
		float bDist = Vector3::DistanceSquared(b.WorldMatrix.Translation(), cameraPosition);
		return aDist > bDist;
	});
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

			break;
		case D3D12_COMMAND_LIST_TYPE_COMPUTE:
			m_CommandListPool[typeIndex].emplace_back(std::make_unique<ComputeCommandContext>(this, static_cast<ID3D12GraphicsCommandList*>(m_CommandLists.back().Get()), pAllocator));
			break;
		case D3D12_COMMAND_LIST_TYPE_COPY:
			m_CommandListPool[typeIndex].emplace_back(std::make_unique<CopyCommandContext>(this, static_cast<ID3D12GraphicsCommandList*>(m_CommandLists.back().Get()), pAllocator));
			break;
		default:
			assert(false);
			break;
		}

		return m_CommandListPool[typeIndex].back().get();
	}
}

D3D12_CPU_DESCRIPTOR_HANDLE Graphics::AllocateCpuDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE type)
{
	return m_DescriptorHeaps[type]->AllocateDescriptor();
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

