#include "stdafx.h"
#include "Graphics.h"
#include "CommandAllocatorPool.h"
#include "CommandQueue.h"
#include "CommandContext.h"
#include "DescriptorAllocator.h"
#include "DynamicResourceAllocator.h"
#include "ImGuiRenderer.h"

#include <filesystem>
#include <stdexcept>
#include <cstddef>
#include <assert.h>

#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#define STB_IMAGE_IMPLEMENTATION
#include "External/stb/stb_image.h"

#include "External/imgui/imgui.h"

Graphics::Graphics(uint32_t width, uint32_t height):
	m_WindowWidth(width), m_WindowHeight(height)
{
}

Graphics::~Graphics()
{
}

void Graphics::Initialize(WindowHandle window)
{
	MakeWindow();
	InitD3D(m_Hwnd);
	OnResize(m_WindowWidth, m_WindowHeight);

	InitializeAssets();

	MSG msg = {};
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			Update();
		}
	}
}

void Graphics::Update()
{
	WaitForFence(m_FenceValues[m_CurrentBackBufferIndex]);

	m_pImGuiRenderer->NewFrame();
	ImGui::ShowDemoWindow();

	CommandContext* pCommandContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	ID3D12GraphicsCommandList* pCommandList = pCommandContext->GetCommandList();
	
	pCommandList->SetPipelineState(m_pPipelineStateObject.Get());
	pCommandList->SetGraphicsRootSignature(m_pRootSignature.Get());

	pCommandContext->SetViewport(m_Viewport);
	pCommandContext->SetScissorRect(m_Viewport);

	pCommandContext->InsertResourceBarrier(CD3DX12_RESOURCE_BARRIER::Transition(
		m_RenderTargets[m_CurrentBackBufferIndex].Get(),
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET), true);

	DirectX::SimpleMath::Color clearColor{ 0.1f, 0.1f, 0.1f, 1.0f };
	pCommandContext->ClearRenderTarget(m_RenderTargetHandles[m_CurrentBackBufferIndex], clearColor);
	pCommandContext->ClearDepth(m_DepthStencilHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);

	pCommandContext->SetRenderTarget(&m_RenderTargetHandles[m_CurrentBackBufferIndex]);
	pCommandContext->SetDepthStencil(&m_DepthStencilHandle);

	SetDynamicConstantBufferView(pCommandContext);

	auto srvDescriptorHeaps = m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->GetDescriptorHeapPool();
	ID3D12DescriptorHeap* ppHeaps[] = { srvDescriptorHeaps[0].Get()};
	pCommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	pCommandList->SetGraphicsRootDescriptorTable(1, m_TextureHandle.GetGpuHandle());

	pCommandContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCommandContext->SetVertexBuffer(m_VertexBufferView);
	pCommandContext->SetIndexBuffer(m_IndexBufferView);
	pCommandContext->DrawIndexed(m_IndexCount, 0);

	m_pImGuiRenderer->Render(*pCommandContext);

	pCommandContext->InsertResourceBarrier(CD3DX12_RESOURCE_BARRIER::Transition(
		m_RenderTargets[m_CurrentBackBufferIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT));

	m_FenceValues[m_CurrentBackBufferIndex] = pCommandContext->Execute(false);

	m_pSwapchain->Present(1, 0);
	m_CurrentBackBufferIndex = m_pSwapchain->GetCurrentBackBufferIndex();
}

void Graphics::Shutdown()
{
	// wait for all the GPU work to finish
	IdleGPU();
}

void Graphics::CreateDescriptorHeaps()
{
	assert(m_DescriptorHeaps.size() == D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES);
	for (size_t i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; i++)
	{
		m_DescriptorHeaps[i] = std::make_unique<DescriptorAllocator>(m_pDevice.Get(), (D3D12_DESCRIPTOR_HEAP_TYPE)i);
	}
}

void Graphics::InitD3D(WindowHandle pWindow)
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

	// 4x msaa
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qualityLevels;
	qualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	qualityLevels.Format = m_RenderTargetFormat;
	qualityLevels.NumQualityLevels = 0;
	qualityLevels.SampleCount = 4;
	HR(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &qualityLevels, sizeof(qualityLevels)));

	m_MsaaQuality = qualityLevels.NumQualityLevels;
	if (m_MsaaQuality <= 0)
	{
		return;
	}

	m_CommandQueues[0] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_DIRECT);

	CreateSwapchain(pWindow);
	CreateDescriptorHeaps();

	m_pDynamicCpuVisibleAllocator = std::make_unique<DynamicResourceAllocator>(m_pDevice.Get(), true, 1024 * 1024);
	m_pImGuiRenderer = std::make_unique<ImGuiRenderer>(this);
}

void Graphics::CreateSwapchain(WindowHandle pWindow)
{
	m_pSwapchain.Reset();

	DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
	swapchainDesc.Width = m_WindowWidth;
	swapchainDesc.Height = m_WindowHeight;
	swapchainDesc.Format = m_RenderTargetFormat;
	swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapchainDesc.BufferCount = FRAME_COUNT;
	swapchainDesc.Scaling = DXGI_SCALING_NONE;
	swapchainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapchainDesc.SampleDesc.Count = 1;  // must set for msaa >= 1, not 0
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
		pWindow,
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
		m_RenderTargets[i].Reset();
	}
	m_pDepthStencilBuffer.Reset();

	// resize the buffers
	HR(m_pSwapchain->ResizeBuffers(
			FRAME_COUNT,
			m_WindowWidth,
			m_WindowHeight,
			m_RenderTargetFormat,
			DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

	m_CurrentBackBufferIndex = 0;

	// recreate the render target views
	for (int i = 0; i < FRAME_COUNT; i++)
	{
		m_RenderTargetHandles[i] = m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_RTV]->AllocateDescriptor();
		HR(m_pSwapchain->GetBuffer(i, IID_PPV_ARGS(&m_RenderTargets[i])));
		m_pDevice->CreateRenderTargetView(m_RenderTargets[i].Get(), nullptr, m_RenderTargetHandles[i]);
	}

	// recreate the depth stencil buffer and view
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Alignment = 0;
	desc.Width = m_WindowWidth;
	desc.Height = m_WindowHeight;
	desc.DepthOrArraySize = 1;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	desc.Format = m_DepthStencilFormat;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;

	D3D12_CLEAR_VALUE clearValue;
	clearValue.Format = m_DepthStencilFormat;
	clearValue.DepthStencil.Depth = 1.0f;
	clearValue.DepthStencil.Stencil = 0;
	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	HR(m_pDevice->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&clearValue,
			IID_PPV_ARGS(m_pDepthStencilBuffer.GetAddressOf())));

	m_DepthStencilHandle = m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_DSV]->AllocateDescriptor();
	m_pDevice->CreateDepthStencilView(m_pDepthStencilBuffer.Get(), nullptr, m_DepthStencilHandle);

	m_Viewport.x = 0;
	m_Viewport.y = 0;
	m_Viewport.height = m_WindowHeight;
	m_Viewport.width = m_WindowWidth;

	m_ScissorRect.x = 0;
	m_ScissorRect.y = 0;
	m_ScissorRect.height = m_WindowHeight;
	m_ScissorRect.width = m_WindowWidth;
}

void Graphics::InitializeAssets()
{
	LoadTexture();
	BuildRootSignature();
	BuildShaderAndInputLayout();
	BuildGeometry();
	BuildPSO();
}

void Graphics::BuildRootSignature()
{
	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc{};

	CD3DX12_ROOT_PARAMETER1 rootParameters[2];
	CD3DX12_DESCRIPTOR_RANGE1 srvRange;

	rootParameters[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_VERTEX);

	srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	rootParameters[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

	D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

	D3D12_STATIC_SAMPLER_DESC samplerDesc = CD3DX12_STATIC_SAMPLER_DESC(0);
	rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, &samplerDesc, rootSignatureFlags);

	ComPtr<ID3DBlob> signature, error;
	HR(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
	HR(m_pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_pRootSignature)));
}

void Graphics::BuildShaderAndInputLayout()
{
#if defined(_DEBUG)
	// shader debugging
	uint32_t compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	uint32_t compileFlags = 0;
#endif
	
	compileFlags |= D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;

	std::vector<std::byte> data = ReadFile("Resources/shaders.hlsl");
	
	ComPtr<ID3DBlob> pErrorBlob;
	D3DCompile2(data.data(), data.size(), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, 0, nullptr, 0, m_pVertexShaderCode.GetAddressOf(), pErrorBlob.GetAddressOf());
	if (pErrorBlob != nullptr)
	{
		std::wstring errorMessage((char*)pErrorBlob->GetBufferPointer(), (char*)pErrorBlob->GetBufferPointer() + pErrorBlob->GetBufferSize());
		std::wcout << errorMessage << std::endl;
		return;
	}

	pErrorBlob.Reset();
	D3DCompile2(data.data(), data.size(), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, 0, nullptr, 0, &m_pPixelShaderCode, &pErrorBlob);
	if (pErrorBlob != nullptr)
	{
		std::wstring errorMessage((char*)pErrorBlob->GetBufferPointer(), (char*)pErrorBlob->GetBufferPointer() + pErrorBlob->GetBufferSize());
		std::wcout << errorMessage << std::endl;
		return;
	}

	// input layout
	m_InputElements.push_back(D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
	m_InputElements.push_back(D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
	m_InputElements.push_back(D3D12_INPUT_ELEMENT_DESC{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
}

void Graphics::BuildGeometry()
{
	struct Vertex
	{
		Vector3 Position;
		Vector2 TexCoord;
		Vector3 Normal;
	};

	Assimp::Importer importer;
	const aiScene* pScene = importer.ReadFile("Resources/Man.dae",
			aiProcess_Triangulate |
			aiProcess_ConvertToLeftHanded |
			aiProcess_GenSmoothNormals |
			aiProcess_CalcTangentSpace |
			aiProcess_LimitBoneWeights);

	std::vector<Vertex> vertices(pScene->mMeshes[0]->mNumVertices);
	for (size_t i = 0; i < vertices.size(); i++)
	{
		Vertex& vertex = vertices[i];
		vertex.Position = *reinterpret_cast<Vector3*>(&pScene->mMeshes[0]->mVertices[i]);
		vertex.TexCoord = *reinterpret_cast<Vector2*>(&pScene->mMeshes[0]->mTextureCoords[0][i]);
		vertex.Normal = *reinterpret_cast<Vector3*>(&pScene->mMeshes[0]->mNormals[i]);
	}

	std::vector<uint32_t> indices(pScene->mMeshes[0]->mNumFaces * 3);
	for (size_t i = 0; i < pScene->mMeshes[0]->mNumFaces; i++)
	{
		for (size_t j = 0; j < 3; j++)
		{
			assert(pScene->mMeshes[0]->mFaces[i].mNumIndices == 3);
			indices[i * 3 + j] = pScene->mMeshes[0]->mFaces[i].mIndices[j];
		}
	}

	CommandContext* pCommandContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	ID3D12GraphicsCommandList* pCommandList = pCommandContext->GetCommandList();

	{
		uint32_t size = vertices.size() * sizeof(Vertex);
		auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(size);
		HR(m_pDevice->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(m_pVertexBuffer.GetAddressOf())));
		pCommandContext->InitializeBuffer(m_pVertexBuffer.Get(), vertices.data(), size);

		m_VertexBufferView.BufferLocation = m_pVertexBuffer->GetGPUVirtualAddress();
		m_VertexBufferView.SizeInBytes = size;
		m_VertexBufferView.StrideInBytes = sizeof(Vertex);
	}

	{
		m_IndexCount = (int)indices.size();
		uint32_t size = sizeof(uint32_t)* indices.size();
		auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(size);
		HR(m_pDevice->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(m_pIndexBuffer.GetAddressOf())));
		pCommandContext->InitializeBuffer(m_pIndexBuffer.Get(), indices.data(), size);

		m_IndexBufferView.BufferLocation = m_pIndexBuffer->GetGPUVirtualAddress();
		m_IndexBufferView.SizeInBytes = size;
		m_IndexBufferView.Format = DXGI_FORMAT_R32_UINT;
	}
	
	pCommandContext->Execute(true);
}

void Graphics::LoadTexture()
{
	std::vector<std::byte> buffer = ReadFile("Resources/Man.png", std::ios::ate | std::ios::binary);
	// if channel 3 , to force to 4
	int width, height, channel;
	void* pPixels = stbi_load_from_memory((unsigned char*)buffer.data(), (int)buffer.size(), &width, &height, &channel, 4);
	if (!pPixels)
	{
		throw std::runtime_error("Failed to load texture: " + std::string(stbi_failure_reason()));
	}

	channel = 4;

	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	auto resDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height);
	HR(m_pDevice->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&resDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(m_pTexture.GetAddressOf())));

	ComPtr<ID3D12Resource> pUploadBuffer;

	auto heapUploadProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	auto resBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(width * height * channel);
	HR(m_pDevice->CreateCommittedResource(
		&heapUploadProps,
		D3D12_HEAP_FLAG_NONE,
		&resBufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(pUploadBuffer.GetAddressOf())));

	D3D12_SUBRESOURCE_DATA	subResourceData{};
	subResourceData.pData = pPixels;
	subResourceData.RowPitch = width * channel;			// row pixels
	subResourceData.SlicePitch = subResourceData.RowPitch;
	
	CommandContext* pContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	ID3D12GraphicsCommandList* pCmd = pContext->GetCommandList();

	// CPU memory -> intermediate upload heap -> GPU memory
	auto resBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
			m_pTexture.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST, 
			D3D12_RESOURCE_STATE_GENERIC_READ);
	UpdateSubresources<1>(pCmd, m_pTexture.Get(), pUploadBuffer.Get(), 0, 0, 1, &subResourceData);
	pCmd->ResourceBarrier(1, &resBarrier);

	pContext->Execute(true);

	stbi_image_free(pPixels);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.PlaneSlice = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;

	m_TextureHandle = m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->AllocateDescriptorWithGPU();
	m_pDevice->CreateShaderResourceView(m_pTexture.Get(), &srvDesc, m_TextureHandle.GetCpuHandle());

	D3D12_SAMPLER_DESC samplerDesc{};
	samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	samplerDesc.Filter = D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR;

	m_SamplerHandle = m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER]->AllocateDescriptor();
	m_pDevice->CreateSampler(&samplerDesc, m_SamplerHandle);
}

void Graphics::BuildPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psDesc = {};
	psDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psDesc.DSVFormat = m_DepthStencilFormat;
	psDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	psDesc.InputLayout.NumElements = (uint32_t)m_InputElements.size();
	psDesc.InputLayout.pInputElementDescs = m_InputElements.data();
	psDesc.NodeMask = 0;
	psDesc.NumRenderTargets = 1;
	psDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psDesc.pRootSignature = m_pRootSignature.Get();
	psDesc.VS = CD3DX12_SHADER_BYTECODE(m_pVertexShaderCode.Get());
	psDesc.PS = CD3DX12_SHADER_BYTECODE(m_pPixelShaderCode.Get());
	psDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psDesc.RTVFormats[0] = m_RenderTargetFormat;
	psDesc.SampleDesc.Count = 1;
	psDesc.SampleDesc.Quality = 0;
	psDesc.SampleMask = UINT_MAX;
	HR(m_pDevice->CreateGraphicsPipelineState(&psDesc, IID_PPV_ARGS(m_pPipelineStateObject.GetAddressOf())));
}

CommandQueue* Graphics::GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const
{
	return m_CommandQueues.at(type).get();
}

CommandContext* Graphics::AllocateCommandContext(D3D12_COMMAND_LIST_TYPE type)
{
	if (m_FreeCommandContexts.size() > 0)
	{
		CommandContext* pCommandContext = m_FreeCommandContexts.front();
		m_FreeCommandContexts.pop();

		pCommandContext->Reset();
		return pCommandContext;
	}
	else
	{
		ComPtr<ID3D12CommandList> pCommandList;
		ID3D12CommandAllocator* pAllocator = m_CommandQueues[type]->RequestAllocator();
		m_pDevice->CreateCommandList(0, type, pAllocator, nullptr, IID_PPV_ARGS(&pCommandList));
		m_CommandLists.push_back(std::move(pCommandList));
		m_CommandListPool.emplace_back(std::make_unique<CommandContext>(this, static_cast<ID3D12GraphicsCommandList*>(m_CommandLists.back().Get()), pAllocator, type));
		return m_CommandListPool.back().get();
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
	m_FreeCommandContexts.push(pCommandContext);
}

void Graphics::IdleGPU()
{
	for (auto& pCommandQueue : m_CommandQueues)
	{
		pCommandQueue->WaitForIdle();
	}
}

void Graphics::SetDynamicConstantBufferView(CommandContext* pCommandContext)
{
	struct ConstantBufferData
	{
		Matrix World;
		Matrix WorldViewProjection;
	} Data;

	Matrix proj = XMMatrixPerspectiveFovLH(
		XM_PIDIV4,
		static_cast<float>(m_WindowWidth) / m_WindowHeight,
		0.1f,
		1000.0f);
	Matrix view = XMMatrixLookAtLH(Vector3(0, 5, 0), Vector3(0, 0, 500), Vector3(0, 1, 0));
	Matrix world = XMMatrixRotationRollPitchYaw(0, GameTimer::GameTime(), 0) * XMMatrixTranslation(0, -50, 500);
	Data.World = world;
	Data.WorldViewProjection = world * view * proj;

	pCommandContext->SetDynamicConstantBufferView(0, &Data, sizeof(ConstantBufferData));
}


void Graphics::MakeWindow()
{
	WNDCLASSW wc;

	wc.hInstance = GetModuleHandle(nullptr);
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hIcon = 0;
	wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	wc.lpfnWndProc = WndProcStatic;
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpszClassName = L"wndClass";
	wc.lpszMenuName = nullptr;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

	if (!RegisterClass(&wc))
	{
		auto error = GetLastError();
		return;
	}

	int displayWidth = GetSystemMetrics(SM_CXSCREEN);
	int displayHeight = GetSystemMetrics(SM_CYSCREEN);

	DWORD windowStyle = WS_OVERLAPPEDWINDOW;

	RECT windowRec = { 0, 0, (LONG)m_WindowWidth, (LONG)m_WindowHeight };
	AdjustWindowRect(&windowRec, windowStyle, false);

	uint32_t windowWidth = windowRec.right - windowRec.left;
	uint32_t windowHeight = windowRec.bottom - windowRec.top;

	int x = (displayWidth - windowWidth) / 2;
	int y = (displayHeight - windowHeight) / 2;

	m_Hwnd = CreateWindow(
		L"wndClass",
		L"Hello World DX12",
		windowStyle,
		x,
		y,
		windowWidth,
		windowHeight,
		nullptr,
		nullptr,
		GetModuleHandle(nullptr),
		this
	);

	if (!m_Hwnd) return;

	ShowWindow(m_Hwnd, SW_SHOWDEFAULT);

	if (!UpdateWindow(m_Hwnd)) return;
}


LRESULT Graphics::WndProcStatic(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	Graphics* pThis = nullptr;
	if (message == WM_NCCREATE)
	{
		pThis = static_cast<Graphics*>(reinterpret_cast<CREATESTRUCT*>(lParam)->lpCreateParams);
		SetLastError(0);
		if (!SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis)))
		{
			if (GetLastError() != 0)
			{
				return 0;
			}
		}
	}
	else
	{
		pThis = reinterpret_cast<Graphics*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
	}

	if (pThis)
	{
		return pThis->WndProc(hWnd, message, wParam, lParam);
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}

LRESULT Graphics::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
		// resize the window
	case WM_SIZE:
		m_WindowWidth = LOWORD(lParam);
		m_WindowHeight = HIWORD(lParam);
		if (m_pDevice)
		{
			if (wParam == SIZE_MINIMIZED)
			{
				mMinimized = true;
				mMaximized = false;
			}
			else if (wParam == SIZE_MAXIMIZED)
			{
				mMinimized = false;
				mMaximized = true;
				OnResize(m_WindowWidth, m_WindowHeight);
			}
			else if (wParam == SIZE_RESTORED)
			{
				// restoring from minimized state
				if (mMinimized)
				{
					mMinimized = false;
					OnResize(m_WindowWidth, m_WindowHeight);
				}
				// restoring from maximized state
				else if (mMaximized)
				{
					mMaximized = false;
					OnResize(m_WindowWidth, m_WindowHeight);
				}
				else if (mResizing)
				{

				}
				else  // api call such as SetWindowPos/ mSwapchain->SetFullscreenState
				{
					OnResize(m_WindowWidth, m_WindowHeight);
				}
			}
			return 0;
		}
	case WM_KEYUP:
		if (wParam == VK_ESCAPE)
		{
			PostQuitMessage(0);
		}
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}