#include "Graphics.h"
#include "LinearAllocator.h"
#include "GpuResource.h"
#include "Timer.h"
#include "D3DUtils.h"
#include <map>

#pragma comment(lib, "dxguid.lib")

Graphics::Graphics(UINT width, UINT height, std::wstring name):
	m_WindowWidth(width), m_WindowHeight(height)
{
}

void Graphics::Initialize()
{
	MakeWindow();
	InitD3D();
	OnResize();

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
			Render();
		}
	}
}

void Graphics::Update()
{
	Timer(L"Update");
	m_CommandAllocators[m_CurrentBackBufferIndex]->Reset();

	m_pCommandList->Reset(m_CommandAllocators[m_CurrentBackBufferIndex].Get(), m_pPipelineStateObject.Get());

	m_pCommandList->SetGraphicsRootSignature(m_pRootSignature.Get());

	ID3D12DescriptorHeap* ppHeaps[] = { m_pCbvSrvHeap.Get() };
	m_pCommandList->SetDescriptorHeaps(1, ppHeaps);
	m_pCommandList->SetGraphicsRootDescriptorTable(0, m_pCbvSrvHeap->GetGPUDescriptorHandleForHeapStart());

	m_pCommandList->RSSetViewports(1, &m_Viewport);
	m_pCommandList->RSSetScissorRects(1, &m_ScissorRect);

	CD3DX12_RESOURCE_BARRIER barrier_present2target = CD3DX12_RESOURCE_BARRIER::Transition(
		m_RenderTargets[m_CurrentBackBufferIndex].Get(), 
		D3D12_RESOURCE_STATE_PRESENT, 
		D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_pCommandList->ResourceBarrier(1, &barrier_present2target);

	auto currentBackBufferView = GetCurrentBackBufferView();
	auto depthStencilView = GetDepthStencilView();
	m_pCommandList->OMSetRenderTargets(1, &currentBackBufferView, true, &depthStencilView);

	const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
	m_pCommandList->ClearRenderTargetView(GetCurrentBackBufferView(), clearColor, 0, nullptr);
	m_pCommandList->ClearDepthStencilView(GetDepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0F, 0, 0, nullptr);

	m_pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_pCommandList->IASetVertexBuffers(0, 1, &m_VertexBufferView);
	m_pCommandList->IASetIndexBuffer(&m_IndexBufferView);
	m_pCommandList->DrawIndexedInstanced(6, 1, 0, 0, 0);

	auto barrier_target2present = CD3DX12_RESOURCE_BARRIER::Transition(
		m_RenderTargets[m_CurrentBackBufferIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT);
	m_pCommandList->ResourceBarrier(1, &barrier_target2present);

	m_pCommandList->Close();
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

void Graphics::Render()
{
	ID3D12CommandList* ppCommandLists[] = { m_pCommandList.Get() };
	m_pCommandQueue->ExecuteCommandLists(1, ppCommandLists);

	m_pSwapchain->Present(1, 0);

	MoveToNextFrame();
}

void Graphics::Shutdown()
{
	WaitForGPU();
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

	unsigned int windowWidth = windowRec.right - windowRec.left;
	unsigned int windowHeight = windowRec.bottom - windowRec.top;

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

void Graphics::InitD3D()
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
	UINT adapter = 0;
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

	// fence
	HR(m_pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_pFence)));

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
	CreateSwapchain();
	CreateRtvAndDsvHeaps();
}

void Graphics::CreateCommandObjects()
{
	// command queue
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	HR(m_pDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_pCommandQueue)));

	//// command allocator
	for (int i = 0; i < m_CommandAllocators.size(); i++)
	{
		HR(m_pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_CommandAllocators[i])));
	}
	
	// command list
	HR(m_pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_CommandAllocators[m_CurrentBackBufferIndex].Get(), nullptr, IID_PPV_ARGS(&m_pCommandList)));
	
	// command lists are created in the recording state, close it before moving on
	HR(m_pCommandList->Close());
}

void Graphics::CreateSwapchain()
{
	m_pSwapchain.Reset();

	DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
	swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapchainDesc.BufferCount = FRAME_COUNT;
	swapchainDesc.Format = m_RenderTargetFormat;
	swapchainDesc.Width = m_WindowWidth;
	swapchainDesc.Height = m_WindowHeight;
	swapchainDesc.Scaling = DXGI_SCALING_NONE;
	swapchainDesc.Flags = 0;
	swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapchainDesc.SampleDesc.Count = 1;  // must set for msaa >= 1, not 0

	HR(m_pFactory->CreateSwapChainForHwnd(
		m_pCommandQueue.Get(),
		m_Hwnd,
		&swapchainDesc,
		nullptr,
		nullptr,
		&m_pSwapchain));
}

void Graphics::WaitForGPU()
{
	// schedule a signal command in the queue
	HR(m_pCommandQueue->Signal(m_pFence.Get(), m_FenceValues[m_CurrentBackBufferIndex]));

	// wait until the fence has been processed
	HANDLE eventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
	HR(m_pFence->SetEventOnCompletion(m_FenceValues[m_CurrentBackBufferIndex], eventHandle));
	WaitForSingleObjectEx(eventHandle, INFINITE, false);
	CloseHandle(eventHandle);

	// increment the fence value for the current frame
	m_FenceValues[m_CurrentBackBufferIndex]++;
}

void Graphics::MoveToNextFrame()
{
	CONST UINT64 currentFenceValue = m_FenceValues[m_CurrentBackBufferIndex];

	m_pCommandQueue->Signal(m_pFence.Get(), currentFenceValue);

	m_CurrentBackBufferIndex = (m_CurrentBackBufferIndex + 1) % 2;

	if (m_pFence->GetCompletedValue() < m_FenceValues[m_CurrentBackBufferIndex])
	{
		HANDLE eventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
		HR(m_pFence->SetEventOnCompletion(m_FenceValues[m_CurrentBackBufferIndex], eventHandle));
		{
			Timer a(L"Wait for next frame");
			WaitForSingleObject(eventHandle, INFINITE);
		}
		CloseHandle(eventHandle);
	}

	m_FenceValues[m_CurrentBackBufferIndex] = currentFenceValue + 1;
}

void Graphics::OnResize()
{
	WaitForGPU();
	m_pCommandList->Reset(m_CommandAllocators[m_CurrentBackBufferIndex].Get(), nullptr);

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

	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
					m_pDepthStencilBuffer.Get(),
					D3D12_RESOURCE_STATE_COMMON,
					D3D12_RESOURCE_STATE_DEPTH_WRITE);
	m_pCommandList->ResourceBarrier(1, &barrier);

	m_pCommandList->Close();
	ID3D12CommandList* pCommandList[] = { m_pCommandList.Get() };
	m_pCommandQueue->ExecuteCommandLists(1, pCommandList);

	WaitForGPU();

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
				OnResize();
			}
			else if (wParam == SIZE_RESTORED)
			{
				// restoring from minimized state
				if (mMinimized)
				{
					mMinimized = false;
					OnResize();
				}
				// restoring from maximized state
				else if (mMaximized)
				{
					mMaximized = false;
					OnResize();
				}
				else if (mResizing)
				{

				}
				else  // api call such as SetWindowPos/ mSwapchain->SetFullscreenState
				{
					OnResize();
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
	m_pCommandList->Reset(m_CommandAllocators[m_CurrentBackBufferIndex].Get(), nullptr);

	BuildDescriptorHeaps();
	BuildConstantBuffers();
	BuildRootSignature();
	BuildShaderAndInputLayout();
	BuildGeometry();
	BuildPSO();

	m_pCommandList->Close();

	ID3D12CommandList* ppCommandLists[] = { m_pCommandList.Get() };
	m_pCommandQueue->ExecuteCommandLists(1, ppCommandLists);

	WaitForGPU();
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
		XMFLOAT4 Color;
	} Data;

	Data.Color = XMFLOAT4(0.0f, 1.0f, 1.0f, 1.0f);

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
	rootParameters[0].InitAsDescriptorTable(1, ranges, D3D12_SHADER_VISIBILITY_ALL);

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
	UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	UINT compileFlags = 0;
#endif

	ComPtr<ID3DBlob> pErrorBlob;

	D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, m_pVertexShaderCode.GetAddressOf(), pErrorBlob.GetAddressOf());
	if (pErrorBlob != nullptr)
	{
		wstring errorMessage((char*)pErrorBlob->GetBufferPointer(), (char*)pErrorBlob->GetBufferPointer() + pErrorBlob->GetBufferSize());
		wcout << errorMessage << endl;
		return;
	}

	pErrorBlob.Reset();
	D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &m_pPixelShaderCode, &pErrorBlob);
	if (pErrorBlob != nullptr)
	{
		wstring errorMessage((char*)pErrorBlob->GetBufferPointer(), (char*)pErrorBlob->GetBufferPointer() + pErrorBlob->GetBufferSize());
		wcout << errorMessage << endl;
		return;
	}

	// input layout
	m_InputElements.push_back(D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
	m_InputElements.push_back(D3D12_INPUT_ELEMENT_DESC{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });

	// shader reflection reference
	ComPtr<ID3D12ShaderReflection> pShaderReflection;
	D3D12_SHADER_DESC shaderDesc;
	D3DReflect(m_pPixelShaderCode->GetBufferPointer(), 
		m_pPixelShaderCode->GetBufferSize(), 
		IID_ID3D11ShaderReflection,
		(void**)pShaderReflection.GetAddressOf());
	pShaderReflection->GetDesc(&shaderDesc);

	std::map<std::string, int> cbRegisterMap;

	for (unsigned int i = 0; i < shaderDesc.BoundResources; i++)
	{
		D3D12_SHADER_INPUT_BIND_DESC resourceDesc;
		pShaderReflection->GetResourceBindingDesc(i, &resourceDesc);

		switch (resourceDesc.Type)
		{
		case D3D_SIT_CBUFFER:
		case D3D_SIT_TBUFFER:
			cbRegisterMap[resourceDesc.Name] = resourceDesc.BindPoint;
		case D3D_SIT_TEXTURE:
		case D3D_SIT_SAMPLER:
		case D3D_SIT_UAV_RWTYPED:
		case D3D_SIT_STRUCTURED:
		case D3D_SIT_UAV_RWSTRUCTURED:
		case D3D_SIT_BYTEADDRESS:
		case D3D_SIT_UAV_RWBYTEADDRESS:
		case D3D_SIT_UAV_APPEND_STRUCTURED:
		case D3D_SIT_UAV_CONSUME_STRUCTURED:
		case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
		default:
			break;
		}
	}

	for (unsigned int c = 0; c < shaderDesc.ConstantBuffers; c++)
	{
		ID3D12ShaderReflectionConstantBuffer* pReflectionConstantBuffer = pShaderReflection->GetConstantBufferByIndex(c);
		D3D12_SHADER_BUFFER_DESC bufferDesc;
		pReflectionConstantBuffer->GetDesc(&bufferDesc);
		unsigned int cbRegister = cbRegisterMap[std::string(bufferDesc.Name)];

		// ...

		for (unsigned int v = 0; v < bufferDesc.Variables; v++)
		{
			ID3D12ShaderReflectionVariable* pVariable = pReflectionConstantBuffer->GetVariableByIndex(v);
			D3D12_SHADER_VARIABLE_DESC variableDesc;
			pVariable->GetDesc(&variableDesc);
			std::string name = variableDesc.Name;

			// ...
		}
	}
}

void Graphics::BuildGeometry()
{
	{
		// vertex buffer
		vector<PosColVertex> vertices =
		{
			PosColVertex({ XMFLOAT3(-1.0f, -1.0f, 0.0f), XMFLOAT4(Colors::White) }),
			PosColVertex({ XMFLOAT3(-1.0f, +1.0f, 0.0f), XMFLOAT4(Colors::Black) }),
			PosColVertex({ XMFLOAT3(+1.0f, +1.0f, 0.0f), XMFLOAT4(Colors::Red) }),
			PosColVertex({ XMFLOAT3(+1.0f, -1.0f, 0.0f), XMFLOAT4(Colors::Red) }),
		};

		auto heap_props_default = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		auto resource_buff_desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(vertices) * sizeof(PosColVertex));
		HR(m_pDevice->CreateCommittedResource(
			&heap_props_default,
			D3D12_HEAP_FLAG_NONE,
			&resource_buff_desc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(m_pVertexBuffer.GetAddressOf())));

		auto heap_props_upload = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		HR(m_pDevice->CreateCommittedResource(
			&heap_props_upload,
			D3D12_HEAP_FLAG_NONE,
			&resource_buff_desc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(pVertexUploadBuffer.GetAddressOf())));

		D3D12_SUBRESOURCE_DATA subResourceData = {};
		subResourceData.pData = vertices.data();
		subResourceData.RowPitch = vertices.size() * sizeof(PosColVertex);
		subResourceData.SlicePitch = subResourceData.RowPitch;

		auto barrier_read2copy = CD3DX12_RESOURCE_BARRIER::Transition(
			m_pVertexBuffer.Get(),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			D3D12_RESOURCE_STATE_COPY_DEST);
		m_pCommandList->ResourceBarrier(1, &barrier_read2copy);

		UpdateSubresources(m_pCommandList.Get(), m_pVertexBuffer.Get(), pVertexUploadBuffer.Get(), 0, 0, 1, &subResourceData);

		auto barrier_copy2read = CD3DX12_RESOURCE_BARRIER::Transition(
			m_pVertexBuffer.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_GENERIC_READ);
		m_pCommandList->ResourceBarrier(1, &barrier_copy2read);

		m_VertexBufferView.BufferLocation = m_pVertexBuffer->GetGPUVirtualAddress();
		m_VertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(PosColVertex) * vertices.size());
		m_VertexBufferView.StrideInBytes = sizeof(PosColVertex);
	}
	
	{
		// index buffer
		vector<unsigned int> indices =
		{
			0, 1, 2, 0, 2, 3
		};

		auto heapProps_default = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		auto resource_buff_desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(indices) * sizeof(unsigned int));
		HR(m_pDevice->CreateCommittedResource(
			&heapProps_default,
			D3D12_HEAP_FLAG_NONE,
			&resource_buff_desc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(m_pIndexBuffer.GetAddressOf())));

		auto heapProps_upload = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		HR(m_pDevice->CreateCommittedResource(
			&heapProps_upload,
			D3D12_HEAP_FLAG_NONE,
			&resource_buff_desc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(pIndexUploadBuffer.GetAddressOf())));

		D3D12_SUBRESOURCE_DATA subResourceData = {};
		subResourceData.pData = indices.data();
		subResourceData.RowPitch = indices.size() * sizeof(unsigned int);
		subResourceData.SlicePitch = subResourceData.RowPitch;

		auto barrier_read2dest = CD3DX12_RESOURCE_BARRIER::Transition(
			m_pIndexBuffer.Get(),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			D3D12_RESOURCE_STATE_COPY_DEST);
		m_pCommandList->ResourceBarrier(1, &barrier_read2dest);

		UpdateSubresources(m_pCommandList.Get(), m_pIndexBuffer.Get(), pIndexUploadBuffer.Get(), 0, 0, 1, &subResourceData);

		auto barrier_dest2read = CD3DX12_RESOURCE_BARRIER::Transition(
			m_pIndexBuffer.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_GENERIC_READ);
		m_pCommandList->ResourceBarrier(1, &barrier_dest2read);

		m_IndexBufferView.BufferLocation = m_pIndexBuffer->GetGPUVirtualAddress();
		m_IndexBufferView.SizeInBytes = static_cast<UINT>(sizeof(unsigned int) * indices.size());
		m_IndexBufferView.Format = DXGI_FORMAT_R32_UINT;
	}
}

void Graphics::BuildPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psDesc = {};
	psDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psDesc.DSVFormat = m_DepthStencilFormat;
	psDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	psDesc.InputLayout.NumElements = m_InputElements.size();
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
