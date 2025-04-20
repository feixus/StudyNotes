#include "stdafx.h"
#include "UWP_Graphics.h"
#include "GpuResource.h"
#include "Timer.h"
#include "CommandAllocatorPool.h"
#include "CommandQueue.h"

#pragma comment(lib, "dxguid.lib")

UWP_Graphics::UWP_Graphics(UINT width, UINT height, std::wstring name) :
	Graphics(width, height, name)
{
	coreWindow = reinterpret_cast<ICoreWindow*>(CoreWindow::GetForCurrentThread());
}

UWP_Graphics::~UWP_Graphics()
{
}

void UWP_Graphics::Initialize()
{
	InitD3D();
	OnResize(m_WindowWidth, m_WindowHeight);

	InitializeAssets();
}

void UWP_Graphics::CreateSwapchain()
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

	ComPtr<IDXGISwapChain1> pSwapChain = nullptr;
	HR(m_pFactory->CreateSwapChainForCoreWindow(
		m_pQueueManager->GetMainCommandQueue()->GetCommandQueue(),
		coreWindow.Get(),
		&swapchainDesc,
		nullptr,
		&pSwapChain));
	pSwapChain.As(&m_pSwapchain);
}

