#include "stdafx.h"
#include "Graphics.h"
#include "CommandAllocatorPool.h"
#include "CommandQueue.h"
#include "SimpleMath/SimpleMath.h"

#pragma comment(lib, "dxguid.lib")

using namespace DirectX;
using namespace DirectX::SimpleMath;

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
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT]->WaitForFenceBlock(m_FenceValues[m_CurrentBackBufferIndex]);

	CommandContext* pCommandContext = AllocatorCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT);
	ID3D12GraphicsCommandList* pCommandList = pCommandContext->pCommandList;
	
	pCommandList->SetPipelineState(m_pPipelineStateObject.Get());
	pCommandList->SetGraphicsRootSignature(m_pRootSignature.Get());

	ID3D12DescriptorHeap* ppHeaps[] = { m_pCbvSrvHeap.Get() };
	pCommandList->SetDescriptorHeaps(1, ppHeaps);
	pCommandList->SetGraphicsRootDescriptorTable(0, m_pCbvSrvHeap->GetGPUDescriptorHandleForHeapStart());

	pCommandList->RSSetViewports(1, &m_Viewport);
	pCommandList->RSSetScissorRects(1, &m_ScissorRect);

	CD3DX12_RESOURCE_BARRIER barrier_present2target = CD3DX12_RESOURCE_BARRIER::Transition(
		m_RenderTargets[m_CurrentBackBufferIndex].Get(), 
		D3D12_RESOURCE_STATE_PRESENT, 
		D3D12_RESOURCE_STATE_RENDER_TARGET);
	pCommandList->ResourceBarrier(1, &barrier_present2target);

	auto currentBackBufferView = GetCurrentBackBufferView();
	auto depthStencilView = GetDepthStencilView();
	pCommandList->OMSetRenderTargets(1, &currentBackBufferView, true, &depthStencilView);

	const float clearColor[] = { 0.1f, 0.1f, 0.1f, 1.0f };
	pCommandList->ClearRenderTargetView(GetCurrentBackBufferView(), clearColor, 0, nullptr);
	pCommandList->ClearDepthStencilView(GetDepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0F, 0, 0, nullptr);

	pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCommandList->IASetVertexBuffers(0, 1, &m_VertexBufferView);
	pCommandList->IASetIndexBuffer(&m_IndexBufferView);
	pCommandList->DrawIndexedInstanced(m_IndexCount, 1, 0, 0, 0);

	auto barrier_target2present = CD3DX12_RESOURCE_BARRIER::Transition(
		m_RenderTargets[m_CurrentBackBufferIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT);
	pCommandList->ResourceBarrier(1, &barrier_target2present);

	m_FenceValues[m_CurrentBackBufferIndex] = ExecuteCommandList(pCommandContext);

	m_pSwapchain->Present(1, 0);

	m_CurrentBackBufferIndex = m_pSwapchain->GetCurrentBackBufferIndex();
}

void Graphics::Shutdown()
{
	// wait for all the GPU work to finish
	IdleGPU();
}

void Graphics::CreateRtvAndDsvHeaps()
{
	// rtv heap
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = FRAME_COUNT;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	HR(m_pDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(m_pRtvHeap.GetAddressOf())));
	
	// dsv heap
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
	dsvHeapDesc.NumDescriptors = FRAME_COUNT;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	HR(m_pDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(m_pDsvHeap.GetAddressOf())));
}

D3D12_CPU_DESCRIPTOR_HANDLE Graphics::GetCurrentBackBufferView() const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(
			m_pRtvHeap->GetCPUDescriptorHandleForHeapStart(),
			m_CurrentBackBufferIndex,
			m_RtvDescriptorSize);
}

D3D12_CPU_DESCRIPTOR_HANDLE Graphics::GetDepthStencilView() const
{
	return m_pDsvHeap->GetCPUDescriptorHandleForHeapStart();
}

ID3D12Resource* Graphics::CurrentBackBuffer() const
{
	return m_RenderTargets[m_CurrentBackBufferIndex].Get();
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

void Graphics::InitD3D(WindowHandle pWindow)
{
#ifdef _DEBUG
	// enable debug
	ComPtr<ID3D12Debug> pDebugController;
	HR(D3D12GetDebugInterface(IID_PPV_ARGS(&pDebugController)));
	pDebugController->EnableDebugLayer();
#endif

	// factory
	HR(CreateDXGIFactory1(IID_PPV_ARGS(&m_pFactory)));

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

	// decriptor sizes
	m_RtvDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	m_DsvDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	m_CbvSrvDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// 4x msaa
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qualityLevels;
	qualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	qualityLevels.Format = m_RenderTargetFormat;
	qualityLevels.NumQualityLevels = 0;
	qualityLevels.SampleCount = 4;
	HR(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &qualityLevels, sizeof(qualityLevels)));

	m_MsaaQuality = qualityLevels.NumQualityLevels;
	if (m_MsaaQuality <= 0) return;

	CreateCommandObjects();
	CreateSwapchain(pWindow);
	CreateRtvAndDsvHeaps();
}

void Graphics::CreateCommandObjects()
{
	// command queue
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT] = std::make_unique<CommandQueue>(m_pDevice.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT);
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

#ifdef PLATFORM_UWP
	HR(m_pFactory->CreateSwapChainForCoreWindow(
		m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT]->GetCommandQueue(), 
		reinterpret_cast<IUnknown*>(pWindow),
		&swapchainDesc,
		nullptr,
		&pSwapChain));
#else
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
#endif

	pSwapChain.As(&m_pSwapchain);
}

void Graphics::OnResize(int width, int height)
{
	m_WindowWidth = width;
	m_WindowHeight = height;

	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT]->WaitForIdle();

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
	CD3DX12_CPU_DESCRIPTOR_HANDLE handle(m_pRtvHeap->GetCPUDescriptorHandleForHeapStart());
	for (int i = 0; i < FRAME_COUNT; i++)
	{
		HR(m_pSwapchain->GetBuffer(i, IID_PPV_ARGS(&m_RenderTargets[i])));
		m_pDevice->CreateRenderTargetView(m_RenderTargets[i].Get(), nullptr, handle);
		handle.Offset(1, m_RtvDescriptorSize);
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
			D3D12_RESOURCE_STATE_COMMON,
			&clearValue,
			IID_PPV_ARGS(m_pDepthStencilBuffer.GetAddressOf())));

	m_pDevice->CreateDepthStencilView(m_pDepthStencilBuffer.Get(), nullptr, GetDepthStencilView());

	CommandContext* pCommandContext = AllocatorCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT);
	ID3D12GraphicsCommandList* pCommandList = pCommandContext->pCommandList;

	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
					m_pDepthStencilBuffer.Get(),
					D3D12_RESOURCE_STATE_COMMON,
					D3D12_RESOURCE_STATE_DEPTH_WRITE);
	pCommandList->ResourceBarrier(1, &barrier);

	ExecuteCommandList(pCommandContext, true);

	m_Viewport.Height = (float)m_WindowHeight;
	m_Viewport.Width = (float)m_WindowWidth;
	m_Viewport.MaxDepth = 1.0f;
	m_Viewport.MinDepth = 0.0f;
	m_Viewport.TopLeftX = 0.0f;
	m_Viewport.TopLeftY = 0.0f;

	m_ScissorRect.left = 0;
	m_ScissorRect.top = 0;
	m_ScissorRect.right = m_WindowWidth;
	m_ScissorRect.bottom = m_WindowHeight;
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

void Graphics::InitializeAssets()
{
	BuildDescriptorHeaps();
	BuildConstantBuffers();
	BuildRootSignature();
	BuildShaderAndInputLayout();
	BuildGeometry();
	BuildPSO();
}

void Graphics::BuildDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	heapDesc.NumDescriptors = 2;
	heapDesc.NodeMask = 0;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	HR(m_pDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(m_pCbvSrvHeap.GetAddressOf())));
}

void Graphics::BuildConstantBuffers()
{
	// constant buffer

	struct ConstantBufferData
	{
		Matrix WorldViewProjection;
	} Data;

	Matrix proj = XMMatrixPerspectiveFovLH(
		XM_PIDIV4,
		static_cast<float>(m_WindowWidth) / static_cast<float>(m_WindowHeight),
		0.001f,
		100.0f);
	Matrix view = XMMatrixLookAtLH(Vector3(0, 0, 0), Vector3(0, 0, 1), Vector3(0, 1, 0));
	Matrix world = XMMatrixTranslation(0, 0, 10);
	Data.WorldViewProjection = world * view * proj;

	// alignment to a 256-byte boundary for constant buffers
	int size = (sizeof(ConstantBufferData) + 255) & ~255;

	CD3DX12_RESOURCE_DESC constantBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(size);
	auto head_props_upload = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	HR(m_pDevice->CreateCommittedResource(
		&head_props_upload,
		D3D12_HEAP_FLAG_NONE,
		&constantBufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_pConstantBuffer)));

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = m_pConstantBuffer->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = size;	
	m_pDevice->CreateConstantBufferView(&cbvDesc, m_pCbvSrvHeap->GetCPUDescriptorHandleForHeapStart());

	// map and initialize the constant buffer. dont unmap this until the app closes
	// keeping things mapped for the lifetime of the resource is okay
	CD3DX12_RANGE readRange(0, 0);  // dont intend to read from this resource on the CPU
	void* pData = nullptr;
	m_pConstantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pData));
	memcpy(pData, &Data, sizeof(Data));
}

void Graphics::BuildRootSignature()
{
	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;

	CD3DX12_ROOT_PARAMETER1 rootParameters[1];
	CD3DX12_DESCRIPTOR_RANGE1 ranges[1];

	ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
	rootParameters[0].InitAsDescriptorTable(1, ranges, D3D12_SHADER_VISIBILITY_VERTEX);

	D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

	rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, nullptr, rootSignatureFlags);

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

	std::string data = "\
 	cbuffer Data : register(b0) \
 	{ \
 		float4x4 WorldViewProjection; \
 	} \
 	struct VSInput \
 	{ \
 		float3 position : POSITION; \
 	}; \
 	struct PSInput \
 	{ \
 		float4 position : SV_POSITION; \
 	}; \
 	PSInput VSMain(VSInput input) \
 	{ \
 		PSInput result; \
 		result.position = mul(float4(input.position, 1.0f), WorldViewProjection); \
 		return result; \
 	} \
 	float4 PSMain(PSInput input) : SV_TARGET \
 	{ \
 		return float4(1,0,1,1); \
 	}";

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
}

void Graphics::BuildGeometry()
{
	CommandContext* pCommandContext = AllocatorCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT);
	ID3D12GraphicsCommandList* pCommandList = pCommandContext->pCommandList;

	ComPtr<ID3D12Resource> pVertexUploadBuffer;
	ComPtr<ID3D12Resource> pIndexUploadBuffer;
	
	// vertex buffer
	std::vector<Vector3> vertices = {
		Vector3(0, 0, 0),
		Vector3(1, 0, 0),
		Vector3(1, 1, 0),
		Vector3(0, 1, 0),
		Vector3(0, 1, 1),
		Vector3(1, 1, 1),
		Vector3(1, 0, 1),
		Vector3(0, 0, 1),
	};

	m_pVertexBuffer = CreateDefaultBuffer(m_pDevice.Get(), pCommandList, vertices.data(), vertices.size() * sizeof(Vector3), pVertexUploadBuffer);

	m_VertexBufferView.BufferLocation = m_pVertexBuffer->GetGPUVirtualAddress();
	m_VertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(XMFLOAT3) * vertices.size());
	m_VertexBufferView.StrideInBytes = sizeof(XMFLOAT3);
	
	// index buffer
	vector<uint32_t> indices = {
		0, 2, 1, //face front
		0, 3, 2,
		2, 3, 4, //face top
		2, 4, 5,
		1, 2, 5, //face right
		1, 5, 6,
		0, 7, 4, //face left
		0, 4, 3,
		5, 4, 7, //face back
		5, 7, 6,
		0, 6, 7, //face bottom
		0, 1, 6
	};

	m_IndexCount = (int)indices.size();
	m_pIndexBuffer = CreateDefaultBuffer(m_pDevice.Get(), pCommandList, indices.data(), indices.size() * sizeof(uint32_t), pIndexUploadBuffer);

	m_IndexBufferView.BufferLocation = m_pIndexBuffer->GetGPUVirtualAddress();
	m_IndexBufferView.SizeInBytes = static_cast<uint32_t>(sizeof(uint32_t) * indices.size());
	m_IndexBufferView.Format = DXGI_FORMAT_R32_UINT;
	
	ExecuteCommandList(pCommandContext, true);
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

CommandQueue* Graphics::GetMainCommandQueue() const
{
	return m_CommandQueues.at(D3D12_COMMAND_LIST_TYPE_DIRECT).get();
}

CommandContext* Graphics::AllocatorCommandList(D3D12_COMMAND_LIST_TYPE type)
{
	uint64_t fenceValue = m_CommandQueues[type]->GetLastCompletedFence();
	ID3D12CommandAllocator* pAllocator = m_CommandQueues[type]->GetAllocatorPool()->GetAllocator(fenceValue);
	if (m_FreeCommandLists.size() > 0)
	{
		CommandContext* pCommandContext = m_FreeCommandLists.front();
		m_FreeCommandLists.pop();
		pCommandContext->pCommandList->Reset(pAllocator, m_pPipelineStateObject.Get());
		pCommandContext->pAllocator = pAllocator;
		return pCommandContext;
	}
	else
	{
		ComPtr<ID3D12CommandList> pCommandList;
		m_pDevice->CreateCommandList(0, type, pAllocator, nullptr, IID_PPV_ARGS(&pCommandList));
		m_CommandLists.push_back(std::move(pCommandList));
		m_CommandListPool.push_back(CommandContext{ static_cast<ID3D12GraphicsCommandList*>(m_CommandLists.back().Get()), pAllocator, type });
		return &m_CommandListPool.back();
	}
}

void Graphics::FreeCommandList(CommandContext * pCommandContext)
{
	m_FreeCommandLists.push(pCommandContext);
}

uint64_t Graphics::ExecuteCommandList(CommandContext* pCommandContext, bool waitForCompletion)
{
	CommandQueue* pOwningQueue = m_CommandQueues[pCommandContext->QueueType].get();
	uint64_t fenceValue = pOwningQueue->ExecuteCommandList(pCommandContext, waitForCompletion);
	FreeCommandList(pCommandContext);
	return fenceValue;
}

void Graphics::IdleGPU()
{
	for (auto& pCommandQueue : m_CommandQueues)
	{
		pCommandQueue.second->WaitForIdle();
	}
}
