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

float RandomRange(float min, float max)
{
	float random = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
	float diff = max - min;
	float r = random * diff;
	return min + r;
}

void Graphics::Initialize(HWND hWnd)
{
	m_pWindow = hWnd;

	InitD3D();
	InitializeAssets();

	m_FrameTimes.resize(256);

	m_CameraPosition = Vector3(0, 100, -15);
	m_CameraRotation = Quaternion::CreateFromYawPitchRoll(XM_PIDIV4, XM_PIDIV4, 0);

	m_Lights.resize(20);
	for (int i = 0; i < m_Lights.size(); i++)
	{
		Vector4 color = Vector4(RandomRange(0, 1), RandomRange(0, 1), RandomRange(0, 1), 1);
		color.Normalize(color);
		m_Lights[i] = Light::Point(Vector3(RandomRange(-200, 200), RandomRange(0, 400), RandomRange(-200, 200)), 120.0f, 1.0f, 0.5f, color);
	}
}

void Graphics::Update()
{
	struct PerFrameData
	{
		Matrix LightViewProjection;
		Matrix ViewInverse;
	} frameData;

	Vector3 mainLightPosition = Vector3(cos(GameTimer::GameTime() / 5.0f), 1.5f, sin(GameTimer::GameTime() / 5.0f)) * 80;
	Vector3 mainLightDirection;
	mainLightPosition.Normalize(mainLightDirection);
	mainLightDirection *= -1;
	m_Lights[0] = Light::Directional(mainLightPosition, mainLightDirection);

	frameData.LightViewProjection = XMMatrixLookAtLH(m_Lights[0].Position, Vector3(0, 0, 0), Vector3(0, 1, 0))
							* XMMatrixOrthographicLH(512, 512, 5.0f, 200);

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

	frameData.ViewInverse = Matrix::CreateFromQuaternion(m_CameraRotation) * Matrix::CreateTranslation(m_CameraPosition);
	Matrix cameraView;
	frameData.ViewInverse.Invert(cameraView);
	Matrix cameraProj = XMMatrixPerspectiveFovLH(XM_PIDIV4, (float)m_WindowWidth / m_WindowHeight, 1.0f, 300);
	Matrix cameraViewProj = cameraView * cameraProj;

	BeginFrame();

	uint64_t nextFenceValue = 0;
	uint64_t lightCullingFence = 0;
	uint64_t clearLightIndexFence = 0;
	uint64_t shadowsFence = 0;

	// reset light index counter
	{
		GraphicsCommandContext* pCommandContext = (GraphicsCommandContext*)AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
		uint32_t zero = 0;
		m_pLightIndexCounterBuffer->SetData(pCommandContext, &zero, sizeof(uint32_t));
		clearLightIndexFence = pCommandContext->Execute(false);
	}

	// frustum generation
	{
		ComputeCommandContext* pCommandContext = (ComputeCommandContext*)AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COMPUTE);
		pCommandContext->SetPipelineState(m_pComputeGenerateFrustumsPipeline.get());
		pCommandContext->SetComputeRootSignature(m_pComputeGenerateFrustumsRootSignature.get());

#pragma pack(push)
#pragma pack(16)
		struct ShaderParameters
		{
			Matrix ProjectionInverse;
			Vector2 ScreenDimensions;
			Vector2 padding;
			uint32_t NumThreadGroups[4];
			uint32_t NumThreads[4];
		} Data;
#pragma pack(pop)

		cameraProj.Invert(Data.ProjectionInverse);
		Data.ScreenDimensions.x = (float)m_WindowWidth;
		Data.ScreenDimensions.y = (float)m_WindowHeight;
		Data.NumThreadGroups[0] = (uint32_t)ceil(m_FrustumCountX / 16.0f);
		Data.NumThreadGroups[1] = (uint32_t)ceil(m_FrustumCountY / 16.0f);
		Data.NumThreadGroups[2] = 1;
		Data.NumThreads[0] = m_FrustumCountX;
		Data.NumThreads[1] = m_FrustumCountY;
		Data.NumThreads[2] = 1;

		pCommandContext->SetDynamicConstantBufferView(0, &Data, sizeof(ShaderParameters));
		pCommandContext->SetDynamicDescriptor(1, 0, m_pFrustumBuffer->GetUAV());
		
		pCommandContext->Dispatch(Data.NumThreadGroups[0], Data.NumThreadGroups[1], Data.NumThreadGroups[2]);
		pCommandContext->Execute(false);
	}

	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_COMPUTE]->InsertWaitForFence(clearLightIndexFence);

	// light culling
	{
		ComputeCommandContext* pCommandContext = (ComputeCommandContext*)AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COMPUTE);
		pCommandContext->SetPipelineState(m_pComputeLightCullPipeline.get());
		pCommandContext->SetComputeRootSignature(m_pComputeLightCullRootSignature.get());

#pragma pack(push)
#pragma pack(16)
		struct ShaderParameter
		{
			Matrix CameraView;
			uint32_t NumThreadGroups[4];
		} Data;

#pragma pack(pop)

		Data.CameraView = cameraView;
		Data.NumThreadGroups[0] = m_FrustumCountX;
		Data.NumThreadGroups[2] = m_FrustumCountY;
		Data.NumThreadGroups[1] = 1;

		pCommandContext->SetDynamicConstantBufferView(0, &Data, sizeof(ShaderParameter));
		pCommandContext->SetDynamicConstantBufferView(1, m_Lights.data(), sizeof(Light) * (uint32_t)m_Lights.size());
		pCommandContext->SetDynamicDescriptor(2, 0, m_pLightIndexCounterBuffer->GetUAV());
		pCommandContext->SetDynamicDescriptor(2, 1, m_pLightIndexListBuffer->GetUAV());
		pCommandContext->SetDynamicDescriptor(2, 2, m_pLightGrid->GetUAV());
		pCommandContext->SetDynamicDescriptor(3, 0, m_pFrustumBuffer->GetSRV());

		pCommandContext->Dispatch(Data.NumThreadGroups[0], Data.NumThreadGroups[1], Data.NumThreadGroups[2]);
		lightCullingFence = pCommandContext->Execute(false);
	}

	// shadow map
	{
		GraphicsCommandContext* pCommandContext = (GraphicsCommandContext*)AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
		pCommandContext->MarkBegin(L"Shadows");
		pCommandContext->SetPipelineState(m_pShadowPipelineStateObject.get());
		pCommandContext->SetGraphicsRootSignature(m_pShadowRootSignature.get());

		pCommandContext->SetViewport(FloatRect(0, 0, (float)m_pShadowMap->GetWidth(), (float)m_pShadowMap->GetHeight()));
		pCommandContext->SetScissorRect(FloatRect(0, 0, (float)m_pShadowMap->GetWidth(), (float)m_pShadowMap->GetHeight()));

		pCommandContext->InsertResourceBarrier(m_pShadowMap.get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
		pCommandContext->SetRenderTargets(nullptr, m_pShadowMap->GetDSV());

		Color clearColor = Color(0.1f, 0.1f, 0.1f, 1.0f);
		pCommandContext->ClearDepth(m_pShadowMap->GetDSV(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);

		pCommandContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		struct PerObjectData
		{
			Matrix WorldViewProjection;
		} ObjectData;
		ObjectData.WorldViewProjection = frameData.LightViewProjection;

		pCommandContext->SetDynamicConstantBufferView(0, &ObjectData, sizeof(ObjectData));
		for (int i = 0; i < m_pMesh->GetMeshCount(); i++)
		{
			m_pMesh->GetMesh(i)->Draw(pCommandContext);
		}

		pCommandContext->InsertResourceBarrier(m_pShadowMap.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, true);
		pCommandContext->MarkEnd();
		
		shadowsFence = pCommandContext->Execute(false);
	}

	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT]->InsertWaitForFence(lightCullingFence);
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT]->InsertWaitForFence(shadowsFence);

	// 3D
	{
		GraphicsCommandContext* pCommandContext = (GraphicsCommandContext*)AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
		pCommandContext->MarkBegin(L"3D");

		pCommandContext->SetPipelineState(m_pPipelineStateObject.get());
		pCommandContext->SetGraphicsRootSignature(m_pRootSignature.get());
		
		pCommandContext->SetViewport(m_Viewport);
		pCommandContext->SetScissorRect(m_Viewport);
	
		pCommandContext->InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET, false);
		pCommandContext->InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, false);
		pCommandContext->InsertResourceBarrier(m_pLightIndexListBuffer.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, true);
		
		DirectX::SimpleMath::Color clearColor{ 0.f, 0.f, 0.f, 1.0f };
		pCommandContext->ClearRenderTarget(GetCurrentRenderTarget()->GetRTV(), clearColor);
		pCommandContext->ClearDepth(GetDepthStencilView()->GetDSV(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);

		auto rtv = GetCurrentRenderTarget()->GetRTV();
		pCommandContext->SetRenderTargets(&rtv, GetDepthStencilView()->GetDSV());

		pCommandContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	
		struct PerObjectData
		{
			Matrix World;
			Matrix WorldViewProjection;
		} objectData;

		objectData.World = XMMatrixIdentity();
		objectData.WorldViewProjection = objectData.World * cameraViewProj;

		pCommandContext->SetDynamicConstantBufferView(0, &objectData, sizeof(PerObjectData));
		pCommandContext->SetDynamicConstantBufferView(1, &frameData, sizeof(PerFrameData));
		pCommandContext->SetDynamicConstantBufferView(2, m_Lights.data(), sizeof(Light) * (uint32_t)m_Lights.size());
		pCommandContext->SetDynamicDescriptor(4, 0, m_pShadowMap->GetSRV());
		pCommandContext->SetDynamicDescriptor(5, 0, m_pLightGrid->GetSRV());
		pCommandContext->SetDynamicDescriptor(5, 1, m_pLightIndexListBuffer->GetSRV());
		for (int i = 0; i < m_pMesh->GetMeshCount(); i++)
		{
			SubMesh* pSubMesh = m_pMesh->GetMesh(i);
			const Material& material = m_pMesh->GetMaterial(pSubMesh->GetMaterialId());

			pCommandContext->SetDynamicDescriptor(3, 0, material.pDiffuseTexture->GetSRV());
			pCommandContext->SetDynamicDescriptor(3, 1, material.pNormalTexture->GetSRV());
			pCommandContext->SetDynamicDescriptor(3, 2, material.pSpecularTexture->GetSRV());

			pSubMesh->Draw(pCommandContext);
		}
		pCommandContext->MarkEnd();

		pCommandContext->InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, false);
		pCommandContext->InsertResourceBarrier(m_pLightIndexListBuffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);

		pCommandContext->Execute(false);
	}

	UpdateImGui();

	GraphicsCommandContext* pCommandContext = (GraphicsCommandContext*)AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	pCommandContext->MarkBegin(L"UI");
	// UI
	{
		m_pImGuiRenderer->Render(*pCommandContext);
	}
	pCommandContext->MarkEnd();

	pCommandContext->MarkBegin(L"Present");
	// present
	{
		if (m_SampleCount > 1)
		{
			pCommandContext->InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE, false);
			pCommandContext->InsertResourceBarrier(GetCurrentBackbuffer(), D3D12_RESOURCE_STATE_RESOLVE_DEST, true);
			pCommandContext->GetCommandList()->ResolveSubresource(GetCurrentBackbuffer()->GetResource(), 0, GetCurrentRenderTarget()->GetResource(), 0, RENDER_TARGET_FORMAT);
		}
		pCommandContext->InsertResourceBarrier(GetCurrentBackbuffer(), D3D12_RESOURCE_STATE_PRESENT, true);
	}
	pCommandContext->MarkEnd();

	nextFenceValue = pCommandContext->Execute(false);
	EndFrame(nextFenceValue);
}

void Graphics::Shutdown()
{
	// wait for all the GPU work to finish
	IdleGPU();
}

uint64_t Graphics::GetFenceToWaitFor()
{
	return (m_CurrentBackBufferIndex + (FRAME_COUNT - 1)) % FRAME_COUNT;
}

void Graphics::BeginFrame()
{
	m_pImGuiRenderer->NewFrame();
}

void Graphics::EndFrame(uint64_t fenceValue)
{
	uint64_t waitFenceIdx = GetFenceToWaitFor();
	WaitForFence(m_FenceValues[waitFenceIdx]);
	m_FenceValues[waitFenceIdx] = fenceValue;

	m_pSwapchain->Present(1, 0);
	m_CurrentBackBufferIndex = m_pSwapchain->GetCurrentBackBufferIndex();

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

	// 4x msaa
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qualityLevels;
	qualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	qualityLevels.Format = RENDER_TARGET_FORMAT;
	qualityLevels.NumQualityLevels = 0;
	qualityLevels.SampleCount = m_SampleCount;
	HR(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &qualityLevels, sizeof(qualityLevels)));
	m_SampleQuality = qualityLevels.NumQualityLevels - 1;

	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_COMPUTE] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_COMPUTE);

	// allocate descriptor heaps pool
	assert(m_DescriptorHeaps.size() == D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES);
	for (size_t i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; i++)
	{
		m_DescriptorHeaps[i] = std::make_unique<DescriptorAllocator>(m_pDevice.Get(), (D3D12_DESCRIPTOR_HEAP_TYPE)i);
	}

	m_pDynamicCpuVisibleAllocator = std::make_unique<DynamicResourceAllocator>(this, true, 0x40000);

	m_pFrustumBuffer = std::make_unique<StructuredBuffer>();
	m_pLightGrid = std::make_unique<GraphicsTexture>();

	// swap chain
	CreateSwapchain();
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

	for (int i = 0; i < FRAME_COUNT; i++)
	{
		m_RenderTargets[i] = std::make_unique<GraphicsTexture>();
		if (m_SampleCount > 1)
		{
			m_MultiSampleRenderTargets[i] = std::make_unique<GraphicsTexture>();
		}
	}
	m_pDepthStencilBuffer = std::make_unique<GraphicsTexture>();
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

	ImGui::Text("MS: %.4f", GameTimer::DeltaTime());
	ImGui::SameLine(100);
	ImGui::Text("FPS: %.1f", 1.0f / GameTimer::DeltaTime());

	ImGui::PlotLines("Frametime", m_FrameTimes.data(), (int)m_FrameTimes.size(), 0, 0, 0.0f, 0.03f, ImVec2(200, 100));

	ImGui::Text("SponzaTime: %.1f", m_LoadSponzaTime);

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

void Graphics::OnResize(int width, int height)
{
	m_WindowWidth = width;
	m_WindowHeight = height;

	IdleGPU();

	for (int i = 0; i < FRAME_COUNT; i++)
	{
		m_RenderTargets[i]->Release();
	}
	m_pDepthStencilBuffer->Release();

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

	m_pDepthStencilBuffer->Create(this, m_WindowWidth, m_WindowHeight, DEPTH_STENCIL_FORMAT, TextureUsage::DepthStencil, m_SampleCount);

	m_FrustumCountX = (int)ceil(m_WindowWidth / 16.0f);
	m_FrustumCountY = (int)ceil(m_WindowHeight / 16.0f);
	m_pFrustumBuffer->Create(this, 64, m_FrustumCountX * m_FrustumCountY, false);

	m_pLightGrid->Create(this, m_FrustumCountX, m_FrustumCountY, DXGI_FORMAT_R32G32_UINT, TextureUsage::UnorderedAccess | TextureUsage::ShaderResource, 1);

	m_Viewport.Left = 0;
	m_Viewport.Top = 0;
	m_Viewport.Right = (float)m_WindowWidth;
	m_Viewport.Bottom = (float)m_WindowHeight;
	m_ScissorRect = m_Viewport;
}

void Graphics::InitializeAssets()
{
	GraphicsCommandContext* pCommandContext = (GraphicsCommandContext*)AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);

	// input layout
	D3D12_INPUT_ELEMENT_DESC inputElements[] = {
			D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			D3D12_INPUT_ELEMENT_DESC{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			D3D12_INPUT_ELEMENT_DESC{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	{
		// shaders
		Shader vertexShader;
		vertexShader.Load("Resources/Diffuse.hlsl", Shader::Type::VertexShader, "VSMain");
		Shader pixelShader;
		pixelShader.Load("Resources/Diffuse.hlsl", Shader::Type::PixelShader, "PSMain");

		// root signature
		m_pRootSignature = std::make_unique<RootSignature>(6);
		m_pRootSignature->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
		m_pRootSignature->SetConstantBufferView(1, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pRootSignature->SetConstantBufferView(2, 2, D3D12_SHADER_VISIBILITY_PIXEL);
		m_pRootSignature->SetDescriptorTableSimple(3, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, D3D12_SHADER_VISIBILITY_PIXEL);
		m_pRootSignature->SetDescriptorTableSimple(4, 3, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, D3D12_SHADER_VISIBILITY_PIXEL);
		m_pRootSignature->SetDescriptorTableSimple(5, 4, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, D3D12_SHADER_VISIBILITY_PIXEL);

		D3D12_SAMPLER_DESC samplerDesc{};
		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
		m_pRootSignature->AddStaticSampler(0, samplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);

		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		samplerDesc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
		m_pRootSignature->AddStaticSampler(1, samplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);

		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		samplerDesc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
		m_pRootSignature->AddStaticSampler(2, samplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);

		D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
		m_pRootSignature->Finalize(m_pDevice.Get(), rootSignatureFlags);

		// pipeline state
		m_pPipelineStateObject = std::make_unique<GraphicsPipelineState>();
		m_pPipelineStateObject->SetInputLayout(inputElements, sizeof(inputElements) / sizeof(inputElements[0]));
		m_pPipelineStateObject->SetRootSignature(m_pRootSignature->GetRootSignature());
		m_pPipelineStateObject->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pPipelineStateObject->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
		m_pPipelineStateObject->SetRenderTargetFormat(RENDER_TARGET_FORMAT, DXGI_FORMAT_D24_UNORM_S8_UINT, m_SampleCount, m_SampleQuality);
		m_pPipelineStateObject->Finalize(m_pDevice.Get());
	}

	{
		Shader vertexShader;
		vertexShader.Load("Resources/Shadows.hlsl", Shader::Type::VertexShader, "VSMain");

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
		m_pShadowPipelineStateObject->SetInputLayout(inputElements, sizeof(inputElements) / sizeof(inputElements[0]));
		m_pShadowPipelineStateObject->SetRootSignature(m_pShadowRootSignature->GetRootSignature());
		m_pShadowPipelineStateObject->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pShadowPipelineStateObject->SetRenderTargetFormats(nullptr, 0, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, 1, 0);
		m_pShadowPipelineStateObject->SetCullMode(D3D12_CULL_MODE_NONE);
		m_pShadowPipelineStateObject->SetDepthBias(0.0f, 0.0f, 4.0f);
		m_pShadowPipelineStateObject->Finalize(m_pDevice.Get());

		m_pShadowMap = std::make_unique<GraphicsTexture>();
		m_pShadowMap->Create(this, 2048, 2048, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, TextureUsage::DepthStencil | TextureUsage::ShaderResource, 1);
	}

	{
		Shader computeShader;
		computeShader.Load("Resources/GenerateFrustums.hlsl", Shader::Type::ComputeShader, "CSMain");

		m_pComputeGenerateFrustumsRootSignature = std::make_unique<RootSignature>(2);
		m_pComputeGenerateFrustumsRootSignature->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
		m_pComputeGenerateFrustumsRootSignature->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pComputeGenerateFrustumsRootSignature->Finalize(m_pDevice.Get(), D3D12_ROOT_SIGNATURE_FLAG_NONE);

		m_pComputeGenerateFrustumsPipeline = std::make_unique<ComputePipelineState>();
		m_pComputeGenerateFrustumsPipeline->SetRootSignature(m_pComputeGenerateFrustumsRootSignature->GetRootSignature());
		m_pComputeGenerateFrustumsPipeline->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pComputeGenerateFrustumsPipeline->Finalize(m_pDevice.Get());

		computeShader.Load("Resources/LightCulling.hlsl", Shader::Type::ComputeShader, "CSMain");

		m_pComputeLightCullRootSignature = std::make_unique<RootSignature>(4);
		m_pComputeLightCullRootSignature->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
		m_pComputeLightCullRootSignature->SetConstantBufferView(1, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pComputeLightCullRootSignature->SetDescriptorTableSimple(2, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, D3D12_SHADER_VISIBILITY_ALL);
		m_pComputeLightCullRootSignature->SetDescriptorTableSimple(3, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pComputeLightCullRootSignature->Finalize(m_pDevice.Get(), D3D12_ROOT_SIGNATURE_FLAG_NONE);

		m_pComputeLightCullPipeline = std::make_unique<ComputePipelineState>();
		m_pComputeLightCullPipeline->SetRootSignature(m_pComputeLightCullRootSignature->GetRootSignature());
		m_pComputeLightCullPipeline->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pComputeLightCullPipeline->Finalize(m_pDevice.Get());

		m_pLightIndexCounterBuffer = std::make_unique<StructuredBuffer>();
		m_pLightIndexCounterBuffer->Create(this, sizeof(uint32_t), 1, false);
		m_pLightIndexListBuffer = std::make_unique<StructuredBuffer>();
		m_pLightIndexListBuffer->Create(this, sizeof(uint32_t), 720000, false);
	}

	// geometry
	GameTimer::CounterBegin();
	m_pMesh = std::make_unique<Mesh>();
	m_pMesh->Load("Resources/sponza/sponza.dae", this, pCommandContext);
	m_LoadSponzaTime = GameTimer::CounterEnd();

	pCommandContext->Execute(true);
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

