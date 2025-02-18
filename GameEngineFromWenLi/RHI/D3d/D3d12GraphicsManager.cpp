#include <objbase.h>
#include <d3dcompiler.h>
#include "D3d12GraphicsManager.hpp"
#include "WindowsApplication.hpp"
#include "directx/d3dx12.h"

using namespace My;

namespace My
{
    extern IApplication* g_pApp;

    template<class T>
    inline void SafeRelease(T **ppInterfaceToRelease)
    {
        if (*ppInterfaceToRelease != nullptr)
        {
            (*ppInterfaceToRelease)->Release();

            (*ppInterfaceToRelease) = nullptr;
        }
    }

    
    static void GetHardwareAdapter(IDXGIFactory7* pFactory, IDXGIAdapter1** ppAdapter)
    {
        IDXGIAdapter1* pAdapter = nullptr;
        *ppAdapter = nullptr;

        for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters1(adapterIndex, &pAdapter); ++adapterIndex)
        {
            DXGI_ADAPTER_DESC1 desc;
            pAdapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                // dont select the basic render driver adapter
                continue;
            }

            // check to see if the adapter supports Direct3D 12, but dont create the actual device yet. 
            if (SUCCEEDED(D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_12_0, _uuidof(ID3D12Device), nullptr)))
            {
                break;
            }
        }

        *ppAdapter = pAdapter;
    }
}

int My::D3d12GraphicsManager::Initialize()
{
    int result = 0;

    result = static_cast<int>(CreateGraphicsResources());

    return result;
}

void My::D3d12GraphicsManager::Finalize()
{
    SafeRelease(&m_pFence);
    SafeRelease(&m_pVertexBuffer);
    SafeRelease(&m_pCommandList);
    SafeRelease(&m_pPipelineState);
    SafeRelease(&m_pRtvHeap);
    SafeRelease(&m_pRootSignature);
    SafeRelease(&m_pCommandQueue);
    SafeRelease(&m_pCommandAllocator);
    for (uint32_t i = 0; i < kFrameCount; i++)
    {
        SafeRelease(&m_pRenderTargets[i]);
    }
    SafeRelease(&m_pSwapChain);
    SafeRelease(&m_pDev);
}

void My::D3d12GraphicsManager::Tick()
{
}

HRESULT My::D3d12GraphicsManager::CreateRenderTarget()
{
    HRESULT hr;
    // describe and create a render target view(RTV) descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{
        .Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
        .NumDescriptors = kFrameCount,
        .Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE
    };
   
    if (FAILED(hr = m_pDev->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_pRtvHeap)))) {
        return hr;
    }

    m_nRtvDescriptorSize = m_pDev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_pRtvHeap->GetCPUDescriptorHandleForHeapStart());

    // create a RTV for each frame
    for (uint32_t i = 0; i < kFrameCount; i++)
    {
        if (FAILED(hr = m_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&m_pRenderTargets[i])))) {
            break;
        }
        m_pDev->CreateRenderTargetView(m_pRenderTargets[i], nullptr, rtvHandle);
        rtvHandle.Offset(1, m_nRtvDescriptorSize);
    }

    return hr;
}

HRESULT My::D3d12GraphicsManager::CreateGraphicsResources()
{
    HRESULT hr;

#if defined(_DEBUG)
        // enable the D3D12 debug layer
        {
            ID3D12Debug* pDebugController;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pDebugController))))
            {
                pDebugController->EnableDebugLayer();
            }
        }
#endif

    IDXGIFactory7* pFactory;
    if (FAILED(hr = CreateDXGIFactory1(IID_PPV_ARGS(&pFactory)))) {
        return hr;
    }

    IDXGIAdapter1* pHardwareAdapter;
    GetHardwareAdapter(pFactory, &pHardwareAdapter);

    if (FAILED(D3D12CreateDevice(pHardwareAdapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_pDev))))
    {
        // roll back to Warp device(Windows Advanced Rasterization Platform)
        IDXGIAdapter4* pWarpAdapter;
        if (FAILED(hr = pFactory->EnumWarpAdapter(IID_PPV_ARGS(&pWarpAdapter)))) {
            SafeRelease(&pFactory);
            return hr;
        }

        if (FAILED(hr = D3D12CreateDevice(pWarpAdapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_pDev)))) {
            SafeRelease(&pFactory);
            return hr;
        }
    }

    HWND hWnd = reinterpret_cast<WindowsApplication*>(g_pApp)->GetMainWindow();

    // describe and create the command queue
    D3D12_COMMAND_QUEUE_DESC queueDesc{
        .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
        .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE
    };

    if (FAILED(hr = m_pDev->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_pCommandQueue)))) {
        SafeRelease(&pFactory);
        return hr;
    }

    // create a struct to hold information about the swap chain
    DXGI_SWAP_CHAIN_DESC1 scd{
        .Width = g_pApp->GetConfiguration().screenWidth,
        .Height = g_pApp->GetConfiguration().screenHeight,
        .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
        .Stereo = FALSE,
        .SampleDesc = {
            .Count = 1,                              // multi-sampling cant be used when in SwapEffect set to DXGI_SWAP_EFFECT_FLOP_DISCARD
            .Quality = 0,                            //above and image quality level    
        },
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,      //how swap chain is to be used
        .BufferCount = kFrameCount,              // back buffer count - double-buffering?
        .Scaling = DXGI_SCALING_STRETCH,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,         // swapping the memory location of back and front buffer, once flipped, discard back buffer content
        .AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
        .Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH     // allow full-screen transition
    };

    IDXGISwapChain1* pSwapChain;
    // swap chain needs the queue so that it can force a flush on it
    if (FAILED(hr = pFactory->CreateSwapChainForHwnd(m_pCommandQueue, hWnd, &scd, nullptr, nullptr, &pSwapChain))) {
        SafeRelease(&pFactory);
        return hr;
    }

    m_pSwapChain = reinterpret_cast<IDXGISwapChain4*>(pSwapChain);

    m_nFrameIndex = m_pSwapChain->GetCurrentBackBufferIndex();
    hr = CreateRenderTarget();

    return hr;
}
