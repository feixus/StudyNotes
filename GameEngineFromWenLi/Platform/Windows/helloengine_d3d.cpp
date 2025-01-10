#include <windows.h>
#include <windowsx.h>
#include <tchar.h>
#include <stdint.h>

#include <d3d11.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <DirectXColors.h>

#include <iostream>

using namespace DirectX;
using namespace DirectX::PackedVector;

const uint32_t SCREEN_WITH = 960;
const uint32_t SCREEN_HEIGHT = 480;

//global declarations
IDXGISwapChain      *g_pSwapchain = nullptr;
ID3D11Device        *g_pDev = nullptr;
ID3D11DeviceContext *g_pDevcon = nullptr;

ID3D11RenderTargetView *g_pRTView = nullptr;

ID3D11InputLayout   *g_pLayout = nullptr;
ID3D11VertexShader  *g_pVS = nullptr;
ID3D11PixelShader   *g_pPS = nullptr;

ID3D11Buffer        *g_pVBuffer = nullptr;

//vertex buffer structure
struct VERTEX {
    XMFLOAT3 Position;
    XMFLOAT4 Color;
};



template<class T>
inline void SafeRelease(T **ppInterfaceToRelease)
{
    if (*ppInterfaceToRelease)
    {
        (*ppInterfaceToRelease)->Release();

        (*ppInterfaceToRelease) = nullptr;
    }
}

void CreateRenderTarget()
{
    ID3D11Texture2D *pBackBuffer;

    // get a pointer to the back buffer
    g_pSwapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);

    // create a render target view
    g_pDev->CreateRenderTargetView(pBackBuffer, NULL, &g_pRTView);

    pBackBuffer->Release();

    // bind the view
    g_pDevcon->OMSetRenderTargets(1, &g_pRTView, NULL);
}

void SetViewPort()
{
    D3D11_VIEWPORT viewport;
    ZeroMemory(&viewport, sizeof(D3D11_VIEWPORT));

    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = SCREEN_WITH;
    viewport.Height = SCREEN_HEIGHT;

    g_pDevcon->RSSetViewports(1, &viewport);
}

// load and prepare the shaders
void InitPipeline()
{
    // load and compile the two shaders
    ID3DBlob *VS, *PS;

    D3DReadFileToBlob(L"copy.vso", &VS);
    D3DReadFileToBlob(L"copy.pso", &PS);

    //encapsulate both shaders into shader objects
    g_pDev->CreateVertexShader(VS->GetBufferPointer(), VS->GetBufferSize(), NULL, &g_pVS);
    g_pDev->CreatePixelShader(PS->GetBufferPointer(), PS->GetBufferSize(), NULL, &g_pPS);

    // set the shader objects
    g_pDevcon->VSSetShader(g_pVS, 0, 0);
    g_pDevcon->PSSetShader(g_pPS, 0, 0);

    // create the input layout object
    D3D11_INPUT_ELEMENT_DESC ied[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };

    g_pDev->CreateInputLayout(ied, 2, VS->GetBufferPointer(), VS->GetBufferSize(), &g_pLayout);
    g_pDevcon->IASetInputLayout(g_pLayout);

    VS->Release();
    PS->Release();
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

    // create the vertex buffer
    D3D11_BUFFER_DESC bd;
    ZeroMemory(&bd, sizeof(bd));

    bd.Usage = D3D11_USAGE_DYNAMIC;     // write access by CPU and GPU
    bd.ByteWidth = sizeof(VERTEX) * 3;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; // allow CPU to write in buffer

    g_pDev->CreateBuffer(&bd, NULL, &g_pVBuffer);

    // copy the vertices into the buffer
    D3D11_MAPPED_SUBRESOURCE ms;
    g_pDevcon->Map(g_pVBuffer, NULL, D3D11_MAP_WRITE_DISCARD, NULL, &ms);   // map the buffer
    memcpy(ms.pData, OurVertices, sizeof(VERTEX) * 3);
    g_pDevcon->Unmap(g_pVBuffer, NULL);
}

// prepare graphic resources
HRESULT CreateGraphicsResources(HWND hWnd)
{
    HRESULT hr = S_OK;

    if (!g_pSwapchain)
    {
        // create a struct to hold information about the swap chain
        DXGI_SWAP_CHAIN_DESC scd;

        //clear out the struct 
        ZeroMemory(&scd, sizeof(DXGI_SWAP_CHAIN_DESC));

        // fill the swap chain description struct
        scd.BufferCount = 1;
        scd.BufferDesc.Width = SCREEN_WITH;
        scd.BufferDesc.Height = SCREEN_HEIGHT;
        scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scd.BufferDesc.RefreshRate.Numerator = 60;
        scd.BufferDesc.RefreshRate.Denominator = 1;
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.OutputWindow = hWnd;
        scd.SampleDesc.Count = 4;
        scd.Windowed = TRUE;
        scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;     // allow full-screen switching

        const D3D_FEATURE_LEVEL FeatureLevels[] = { D3D_FEATURE_LEVEL_11_1,
                                                    D3D_FEATURE_LEVEL_11_0,
                                                    D3D_FEATURE_LEVEL_10_1,
                                                    D3D_FEATURE_LEVEL_10_0,
                                                    D3D_FEATURE_LEVEL_9_3,
                                                    D3D_FEATURE_LEVEL_9_2,
                                                    D3D_FEATURE_LEVEL_9_1};
        D3D_FEATURE_LEVEL FeatureLevelSupported;

        // create a device, device context and swap chain
        hr = D3D11CreateDeviceAndSwapChain(NULL,
                                        D3D_DRIVER_TYPE_HARDWARE,
                                        NULL,
                                        0,
                                        FeatureLevels,
                                        _countof(FeatureLevels),
                                        D3D11_SDK_VERSION,
                                        &scd,
                                        &g_pSwapchain,
                                        &g_pDev,
                                        &FeatureLevelSupported,
                                        &g_pDevcon);

        if (hr == E_INVALIDARG)
        {
            hr = D3D11CreateDeviceAndSwapChain(NULL,
                                        D3D_DRIVER_TYPE_HARDWARE,
                                        NULL,
                                        0,
                                        &FeatureLevelSupported,
                                        1,
                                        D3D11_SDK_VERSION,
                                        &scd,
                                        &g_pSwapchain,
                                        &g_pDev,
                                        NULL,
                                        &g_pDevcon);
        }

        if (hr == S_OK)
        {
            CreateRenderTarget();
            SetViewPort();
            InitPipeline();
            InitGraphics();
        }                                    
    }

    return hr;
}

void DiscardGraphicsResources()
{
    SafeRelease(&g_pLayout);
    SafeRelease(&g_pVS);
    SafeRelease(&g_pPS);
    SafeRelease(&g_pVBuffer);
    SafeRelease(&g_pSwapchain);
    SafeRelease(&g_pRTView);
    SafeRelease(&g_pDev);
    SafeRelease(&g_pDevcon);
}

// render a single frame
void RenderFrame()
{
    //clear the back buffer to a deep blue
    const FLOAT clearColor[] = {0.0f, 0.2f, 0.4f, 1.0f};
    g_pDevcon->ClearRenderTargetView(g_pRTView, clearColor);

    // do 3D rendering on the back buffer
    {
        // select which vertex buffer to display
        UINT stride = sizeof(VERTEX);
        UINT offset = 0;
        g_pDevcon->IASetVertexBuffers(0, 1, &g_pVBuffer, &stride, &offset);

        // select which primitive type
        g_pDevcon->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // draw the vertex buffer to the back buffer
        g_pDevcon->Draw(3, 0);
    }

    // swap the back buffer and the front buffer
    g_pSwapchain->Present(0, 0);
}

// the WindowProc function prototype
LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstances, LPTSTR lpCmdLine, int nCmdShow)
{
    //the handle for the window
    HWND hWnd;
    //holds informatins for the window class
    WNDCLASSEX wc;

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
        SCREEN_WITH, //width of the window
        SCREEN_HEIGHT, //height of the window
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
            result = CreateGraphicsResources(hWnd);
            RenderFrame();
            wasHandled = true;
            break;
        
        case WM_SIZE:
            if (g_pSwapchain)
            {
                DiscardGraphicsResources();
            }
            wasHandled = true;
            break;

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


/* x64 Native Tools Command Prompt for VS 2022

   fxc /T vs_5_0 /Zi /Fo copy.vso copy.vs
   fxc /T ps_5_0 /Zi /Fo copy.pso copy.ps

debug:
1. clang-cl -c -Z7 -o helloengine_d3d.obj helloengine_d3d.cpp
2. link -debug user32.lib d3d11.lib d3dcompiler.lib helloengine_d3d.obj

release:
clang -l user32.lib -l d3d11.lib -l d3dcompiler.lib -o helloengine_d3d.exe helloengine_d3d.cpp
*/