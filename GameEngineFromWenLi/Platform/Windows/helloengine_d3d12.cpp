#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <tchar.h>
#include <stdint.h>

#include "directx/d3dx12.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <DirectXColors.h>

#include <wrl/client.h>

#include <string>
#include <exception>

namespace My
{
    class com_exception : public std::exception
    {
    public:
        com_exception(HRESULT hr) : result(hr) {}

        virtual const char* what() const override
        {
            static char s_str[64] = { 0 };
            sprintf_s(s_str, "Failure with HRESULT of %08X", static_cast<unsigned int>(result));
            return s_str;
        }
    private:
        HRESULT result;
    };

    inline void ThrowIfFailed(HRESULT hr)
    {
        if (FAILED(hr))
        {
            throw com_exception(hr);
        }
    }
}

using namespace My;
using namespace DirectX;
using namespace DirectX::PackedVector;
using namespace Microsoft::WRL;
using namespace std;

const uint32_t nScreenWidth = 960;
const uint32_t nScreenHeight = 480;

const uint32_t nFrameCount = 2;

const bool bUseWarpDevice = true;

//global declarations
D3D12_VIEWPORT g_ViewPort = {0.0f, 0.0f, static_cast<float>(nScreenWidth), static_cast<float>(nScreenHeight)};
D3D12_RECT     g_ScissorRect = {0, 0, nScreenWidth, nScreenHeight};

ComPtr<IDXGISwapChain4>        g_pSwapChain = nullptr;
ComPtr<ID3D12Device>           g_pDev = nullptr;
ComPtr<ID3D12Resource>         g_pRenderTargets[nFrameCount];
ComPtr<ID3D12CommandAllocator> g_pCommandAllocator;
ComPtr<ID3D12CommandQueue>     g_pCommandQueue;
ComPtr<ID3D12RootSignature>    g_pRootSignature;  // a graphics root signature defines what resources are bound to the pipeline
ComPtr<ID3D12DescriptorHeap>   g_pRtvHeap;        // an array of descriptors of GPU objects
ComPtr<ID3D12PipelineState>    g_pPipelineState;  // an object maintains the state of all currently  set shaders
                                                    // and certain fixed function state objects
                                                    // such as the input assembler, tesselator, rasterizer and output manager
ComPtr<ID3D12GraphicsCommandList> g_pCommandList; // a list to store GPU commands, which will be submitted to GPU to execute when done

uint32_t g_nRtvDescriptorSize;

ComPtr<ID3D12Resource>   g_pVertexBuffer;
D3D12_VERTEX_BUFFER_VIEW g_VertexBufferView;

// synchronization objects
uint32_t            g_nFrameIndex;
HANDLE              g_hFenceEvent;
ComPtr<ID3D12Fence> g_pFence;
uint16_t            g_nFenceValue;

//vertex buffer structure
struct VERTEX {
    XMFLOAT3 Position;
    XMFLOAT4 Color;
};

wstring g_AssetsPath;

std::wstring GetAssetFullPath(LPCWSTR assetName)
{
    return g_AssetsPath + assetName;
}

void GetAssetsPath(WCHAR* path, UINT pathSize)
{
    if (path == nullptr)
    {
        throw std::exception();
    }

    DWORD size = GetModuleFileNameW(nullptr, path, pathSize);
    if (size == 0 || size == pathSize)
    {
        throw std::exception();
    }

    WCHAR* lastSlash = wcsrchr(path, L'\\');
    if (lastSlash)
    {
        *(lastSlash + 1) = L'\0';
    }
}

void WaitForPreviousFrame()
{
    // waiting for the frame to complete before continuing is not best practice.
    // this is code implemented as such for simplicity.
    // more advanced samples illustrate how to use fences for efficient resource usage

    // signal and increment the fence value
    const uint64_t fence = g_nFenceValue;
    ThrowIfFailed(g_pCommandQueue->Signal(g_pFence.Get(), fence));
    g_nFenceValue++;

    // wait until the previous frame is finished
    if (g_pFence->GetCompletedValue() < fence)
    {
        ThrowIfFailed(g_pFence->SetEventOnCompletion(fence, g_hFenceEvent));
        WaitForSingleObject(g_hFenceEvent, INFINITE);
    }

    g_nFrameIndex = g_pSwapChain->GetCurrentBackBufferIndex();
}

void CreateRenderTarget()
{
    // describe and create a render target view (RTV) descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = nFrameCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(g_pDev->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&g_pRtvHeap)));

    g_nRtvDescriptorSize = g_pDev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(g_pRtvHeap->GetCPUDescriptorHandleForHeapStart());

    // create a RTV for each frame
    for (uint32_t i = 0; i < nFrameCount; i++)
    {
        ThrowIfFailed(g_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&g_pRenderTargets[i])));
        g_pDev->CreateRenderTargetView(g_pRenderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, g_nRtvDescriptorSize);
    }
}

// load and prepare the shaders
void InitPipeline()
{
    // create a command allocator
    ThrowIfFailed(g_pDev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_pCommandAllocator)));

    // create an empty root signature
    CD3DX12_ROOT_SIGNATURE_DESC rsd;
    rsd.Init(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
    ThrowIfFailed(g_pDev->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&g_pRootSignature)));

    // load the shaders
#if defined(_DEBUG)
    // enable better shader debugging with the graphics debugging tools
    UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    UINT compileFlags = 0;
#endif

    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> pixelShader;

    D3DCompileFromFile(
        GetAssetFullPath(L"copy.vs").c_str(),
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "main",
        "vs_5_0",
        compileFlags,
        0,
        &vertexShader,
        &error);
    if (error)
    {
        OutputDebugString((LPCTSTR)error->GetBufferPointer());
        error->Release();
        throw std::exception();
    }

    D3DCompileFromFile(
        GetAssetFullPath(L"copy.ps").c_str(),
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "main",
        "ps_5_0",
        compileFlags,
        0,
        &pixelShader,
        &error);
    if (error)
    {
        OutputDebugString((LPCTSTR)error->GetBufferPointer());
        error->Release();
        throw std::exception();
    }
    
    // create the input layout object
    D3D12_INPUT_ELEMENT_DESC ied[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    // describe and create the graphics pipeline state object (PSO)
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psod = {};
    psod.InputLayout = {ied, _countof(ied)};
    psod.pRootSignature = g_pRootSignature.Get();
    psod.VS = { reinterpret_cast<UINT8*>(vertexShader->GetBufferPointer()), vertexShader->GetBufferSize() };
    psod.PS = { reinterpret_cast<UINT8*>(pixelShader->GetBufferPointer()), pixelShader->GetBufferSize() };
    psod.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psod.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psod.DepthStencilState.DepthEnable = FALSE;
    psod.DepthStencilState.StencilEnable = FALSE;
    psod.SampleMask = UINT_MAX;
    psod.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psod.NumRenderTargets = 1;
    psod.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psod.SampleDesc.Count = 1;
    ThrowIfFailed(g_pDev->CreateGraphicsPipelineState(&psod, IID_PPV_ARGS(&g_pPipelineState)));

    ThrowIfFailed(g_pDev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_pCommandAllocator.Get(), g_pPipelineState.Get(), IID_PPV_ARGS(&g_pCommandList)));

    ThrowIfFailed(g_pCommandList->Close());
}


// creates the shape to render
void InitGraphics()
{
    // create a triangle using the VERTEX struct
    VERTEX OurVertices[] =
    {
        {XMFLOAT3(0.0f, 0.5f, 0.0f), XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f)},
        {XMFLOAT3(0.45f, -0.5f, 0.0f), XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f)},
        {XMFLOAT3(-0.45f, -0.5f, 0.0f), XMFLOAT4(0.0f, 0.0f, 1.0f, 1.0f)},
    };

    const UINT vertexBufferSize = sizeof(OurVertices);

    // using upload heaps to transfer static data like vert buffers is not recommended.
    // every time the GPU needs it, the upload heap will be marshalled over.
    // please read up on Default Heap usage.
    //an upload heap is used here for code simplicity and because there are very few verts to actually transfer. 
    ThrowIfFailed(g_pDev->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&g_pVertexBuffer)));

    // copy the vertices into the buffer
    uint8_t *pVertexDataBegin;
    CD3DX12_RANGE readRange(0, 0);
    ThrowIfFailed(g_pVertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
    memcpy(pVertexDataBegin, OurVertices, vertexBufferSize);
    g_pVertexBuffer->Unmap(0, nullptr);

    // initialize the vertex buffer view
    g_VertexBufferView.BufferLocation = g_pVertexBuffer->GetGPUVirtualAddress();
    g_VertexBufferView.StrideInBytes = sizeof(VERTEX);
    g_VertexBufferView.SizeInBytes = vertexBufferSize;

    // create synchronization objects and wait until assets have been uploaded to the GPU
    ThrowIfFailed(g_pDev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_pFence)));
    g_nFenceValue = 1;

    // create an event handle to use for frame synchronization
    g_hFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (g_hFenceEvent == nullptr)
    {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    }
   
   // wait for the command list to execute
   // reusing the same command list in our main loop but for now, just want to wait for setup to complete before continuing
   WaitForPreviousFrame();
}

void GetHardwareAdapter(IDXGIFactory7* pFactory, IDXGIAdapter1** ppAdapter)
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

// prepare graphic resources
void CreateGraphicsResources(HWND hWnd)
{
    if (g_pSwapChain.Get() == nullptr)
    {
#if defined(_DEBUG)
    // enable the D3D12 debug layer
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();
        }
    }
#endif

        ComPtr<IDXGIFactory7> factory;
        ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

        if (bUseWarpDevice)
        {
            ComPtr<IDXGIAdapter4> warpAdapter;
            ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

            ThrowIfFailed(D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&g_pDev)));
        }
        else
        {
            ComPtr<IDXGIAdapter1> hardwareAdapter;
            GetHardwareAdapter(factory.Get(), &hardwareAdapter);

            ThrowIfFailed(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&g_pDev)));
        }

        // describe and create the command queue
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

        ThrowIfFailed(g_pDev->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&g_pCommandQueue)));

        // create a struct to hold information about the swap chain
        DXGI_SWAP_CHAIN_DESC1 scd;

        // clear out the struct for use
        ZeroMemory(&scd, sizeof(DXGI_SWAP_CHAIN_DESC1));

        // fill the swap chain description struct
        scd.BufferCount = nFrameCount;              // back buffer count - double-buffering?
        scd.Width = nScreenWidth;
        scd.Height = nScreenHeight;
        scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;      //how swap chain is to be used
        scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;         // swapping the memory location of back and front buffer, once flipped, discard back buffer content
        scd.SampleDesc.Count = 1;
        scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;     // allow full-screen transition

        ComPtr<IDXGISwapChain1> swapChain;
        // swap chain needs the queue so that it can force a flush on it
        ThrowIfFailed(factory->CreateSwapChainForHwnd(g_pCommandQueue.Get(), hWnd, &scd, nullptr, nullptr, &swapChain));

        ThrowIfFailed(swapChain.As(&g_pSwapChain));

        g_nFrameIndex = g_pSwapChain->GetCurrentBackBufferIndex();
        CreateRenderTarget();
        InitPipeline();
        InitGraphics();
    }
}

void DiscardGraphicsResources()
{
    WaitForPreviousFrame();

    CloseHandle(g_hFenceEvent);
}

void PopulateCommandList()
{
    // command list allocators can only be reset when the associated command lists have finished execution on the GPU
    // apps should use fences to determine GPU execution progress
    ThrowIfFailed(g_pCommandAllocator->Reset());   

    // when ExecuteCommandList() is called on a particular command list, that command list can then be reset at any time and must be before re-recording
    ThrowIfFailed(g_pCommandList->Reset(g_pCommandAllocator.Get(), g_pPipelineState.Get()));

    // set necessary state
    g_pCommandList->SetGraphicsRootSignature(g_pRootSignature.Get());
    g_pCommandList->RSSetViewports(1, &g_ViewPort);
    g_pCommandList->RSSetScissorRects(1, &g_ScissorRect);

    // indicate that the back buffer will be used as a render target. 
    g_pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                g_pRenderTargets[g_nFrameIndex].Get(),
                D3D12_RESOURCE_STATE_PRESENT,
                D3D12_RESOURCE_STATE_RENDER_TARGET));
    
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(g_pRtvHeap->GetCPUDescriptorHandleForHeapStart(), g_nFrameIndex, g_nRtvDescriptorSize);
    g_pCommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // clear the back buffer to a deep blue
    const FLOAT clearColor[] = {0.0f, 0.2f, 0.4f, 1.0f};
    g_pCommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    // do 3D rendering on the back buffer here 
    {
        // select which vertex buffer to display
        g_pCommandList->IASetVertexBuffers(0, 1, &g_VertexBufferView);

        // select which primitive type we are using
        g_pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // draw the vertex buffer to the back buffer
        g_pCommandList->DrawInstanced(3, 1, 0, 0);
    }

    // indicate that the back buffer will now be used to present
    g_pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                g_pRenderTargets[g_nFrameIndex].Get(),
                D3D12_RESOURCE_STATE_RENDER_TARGET,
                D3D12_RESOURCE_STATE_PRESENT));

    ThrowIfFailed(g_pCommandList->Close());
}


// render a single frame
void RenderFrame()
{
    // record all the commands we need to render the scene into the command list
    PopulateCommandList();

    // execute the command list
    ID3D12CommandList * ppCommandLists[] = { g_pCommandList.Get() };
    g_pCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // swap the back buffer and the front buffer
    ThrowIfFailed(g_pSwapChain->Present(1, 0));

    WaitForPreviousFrame();
}

// the WindowProc function prototype
LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstances, LPTSTR lpCmdLine, int nCmdShow)
{
    //the handle for the window
    HWND hWnd;
    //holds informatins for the window class
    WNDCLASSEX wc;

    WCHAR assetsPath[512];
    GetAssetsPath(assetsPath, _countof(assetsPath));
    g_AssetsPath = assetsPath;

    //clear out the window class for use
    ZeroMemory(&wc, sizeof(WNDCLASSEX));

    //fill in the struct
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)COLOR_WINDOW;
    wc.lpszClassName = _T("WindowClass1");

    //register the window class
    RegisterClassEx(&wc);

    //create the window and use the result as the handle
    hWnd = CreateWindowEx(0,
        _T("WindowClass1"), //name of the window class
        _T("Hello, Engine![Direct 3D]"), //title of the window
        WS_OVERLAPPEDWINDOW, //window style
        100, //x-position of the window
        100, //y-position of the window
        nScreenWidth, //width of the window
        nScreenHeight, //height of the window
        NULL, //we have no parent window, nullptr
        NULL, //we aren't using menus, nullptr
        hInstance, //application handle
        NULL); //used with multiple windows, nullptr

    //display the window on the screen
    ShowWindow(hWnd, nCmdShow);

    //enter the main loop

    //holds windows event messages
    MSG msg;

    //wait for the next message in the queue, store the result in 'msg'
    while(GetMessage(&msg, nullptr, 0, 0))
    {
        //translate keystroke messages into the right format
        TranslateMessage(&msg);

        //send the message to the WindowProc function
        DispatchMessage(&msg);
    }

   //return this part of the WM_QUIT message to Windows
    return msg.wParam;
}

//the main message handler for the program
LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    LRESULT result = 0;
    bool wasHandled = false;

    //sort through and find what code to run for the message given
    switch(message)
    {
        case WM_CREATE:
            wasHandled = true;
            break;

        case WM_PAINT:
            CreateGraphicsResources(hWnd);
            RenderFrame();
            wasHandled = true;
            break;
        
        // 屏幕拉伸会崩溃的原因
        // case WM_SIZE:
        //     if (g_pSwapChain)
        //     {
        //         DiscardGraphicsResources();
        //         g_pSwapChain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
        //     }
        //     wasHandled = true;
        //     break;

        case WM_DESTROY:
            DiscardGraphicsResources();
            PostQuitMessage(0);
            wasHandled = true;
            break;

        case WM_DISPLAYCHANGE:
            InvalidateRect(hWnd, nullptr, false);
            wasHandled = true;
            break;
    }

    //handle any messages the switch statement didn't
    if (!wasHandled)
    {
        result = DefWindowProc(hWnd, message, wParam, lParam);
    }

    return result;
}

/*
1. descriptor heaps:
    in order to render with resources like textures, DX12 uses a descriptor to provide the meta-data required.
this helper provides a simple manager for the GPU memory heap of resource descriptors.

    D3D12_DESCRIPTOR_HEAP_TYPE_RTV : a heap of render target views
    D3D12_DESCRIPTOR_HEAP_TYPE_DSV : a heap of depth/stencil views
    D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER : a heap of (non-static) samplers
    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV : a heap of constant buffer views, shader resource views, and/or unordered-access views.

    https://github.com/microsoft/DirectXTK12/wiki/DescriptorHeap
    https://cocoa-programing.hatenablog.com/entry/2018/11/21/%E3%80%90DirectX12%E3%80%91%E3%83%87%E3%82%B9%E3%82%AF%E3%83%AA%E3%83%97%E3%82%BF%E3%83%92%E3%83%BC%E3%83%97%E3%81%AE%E4%BD%9C%E6%88%90%E3%80%90%E5%88%9D%E6%9C%9F%E5%8C%96%E3%80%91

2. IID_PPV_ARGS 
    a macro that simplifies the process of querying interfaces from COM(Component Object Model) objects.
the macro helps convert a pointer to a GUID(interface identifier) into the correct interface pointer type for use in function calls. 
*/


/* Developer Command Prompt for VS 2022

cl /EHsc helloengine_d3d12.cpp user32.lib d3d12.lib dxgi.lib d3dcompiler.lib

devenv /debug helloengine_d3d12.exe

*/