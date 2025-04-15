
#include <windows.h>
#include <windowsx.h>
#include <tchar.h>
#include "D2dGraphicsManager.hpp"
#include "WindowsApplication.hpp"

namespace My
{
    extern IApplication* g_pApp;

    template<class T>
    inline void SafeRelease(T **ppInterfaceToRelease)
    {
        if (*ppInterfaceToRelease)
        {
            (*ppInterfaceToRelease)->Release();

            (*ppInterfaceToRelease) = nullptr;
        }
    }

    int D2dGraphicsManager::Initialize()
    {
        int result = 0;

        if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)))
            return -1;
       
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pFactory)))
            return -1;
        
        result = static_cast<int>(CreateGraphicsResources());

        return result;
    }

    void My::D2dGraphicsManager::Finalize()
    {
        DiscardGraphicsResources();
    }

    void My::D2dGraphicsManager::DiscardGraphicsResources()
    {
        SafeRelease(&m_pLightSlateGrayBrush);
        SafeRelease(&m_pCornflowerBlueBrush);
        SafeRelease(&m_pRenderTarget);
        SafeRelease(&m_pFactory);

        CoUninitialize();
    }

    void D2dGraphicsManager::Tick()
    {
    }

    void D2dGraphicsManager::DrawRect()
    {
        HWND hWnd = reinterpret_cast<WindowsApplication*>(g_pApp)->GetMainWindow();

        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);

        // start build GPU draw command
        m_pRenderTarget->BeginDraw();

        // clear the background with white color
        m_pRenderTarget->Clear(D2D1::ColorF(D2D1::ColorF::White));

        //retrieve the size of drawing area
        D2D1_SIZE_F rtSize = m_pRenderTarget->GetSize();
        
        // draw a grid background
        int width = static_cast<int>(rtSize.width);
        int height = static_cast<int>(rtSize.height);

        for (int x = 0; x < width; x += 10)
        {
            m_pRenderTarget->DrawLine(
                D2D1::Point2F(static_cast<FLOAT>(x), 0.0f),
                D2D1::Point2F(static_cast<FLOAT>(x), rtSize.height),
                m_pLightSlateGrayBrush,
                0.5f);
        }

        for (int y = 0; y < height; y += 10)
        {
            m_pRenderTarget->DrawLine(
                D2D1::Point2F(0.0f, static_cast<FLOAT>(y)),
                D2D1::Point2F(rtSize.width, static_cast<FLOAT>(y)),
                m_pLightSlateGrayBrush,
                0.5f);
        }

        // draw two rectangles
        D2D1_RECT_F rectangle1 = D2D1::RectF(
            rtSize.width / 2 - 50.0f,
            rtSize.height / 2 - 50.0f,
            rtSize.width / 2 + 50.0f,
            rtSize.height / 2 + 50.0f);

        D2D1_RECT_F rectangle2 = D2D1::RectF(
            rtSize.width / 2 - 100.0f,
            rtSize.height / 2 - 100.0f,
            rtSize.width / 2 + 100.0f,
            rtSize.height / 2 + 100.0f);
        
        // draw a filled rectangle
        m_pRenderTarget->FillRectangle(&rectangle1, m_pLightSlateGrayBrush);

        // draw the outline of the rectangle
        m_pRenderTarget->DrawRectangle(&rectangle2, m_pCornflowerBlueBrush);

        // end GPU draw command building
        HRESULT hr = m_pRenderTarget->EndDraw();
        if (FAILED(hr) || hr == D2DERR_RECREATE_TARGET)
        {
            DiscardGraphicsResources();
        }

        EndPaint(hWnd, &ps);
        
    }

    HRESULT My::D2dGraphicsManager::CreateGraphicsResources()
    {
        HWND hWnd = reinterpret_cast<WindowsApplication*>(g_pApp)->GetMainWindow();
    
        HRESULT hr = S_OK;

        if (!m_pRenderTarget)
        {
            RECT rc;
            GetClientRect(hWnd, &rc);

            D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

            hr = m_pFactory->CreateHwndRenderTarget(
                D2D1::RenderTargetProperties(),
                D2D1::HwndRenderTargetProperties(hWnd, size),
                &m_pRenderTarget);

            if (SUCCEEDED(hr))
            {
                hr = m_pRenderTarget->CreateSolidColorBrush(
                    D2D1::ColorF(D2D1::ColorF::LightSlateGray),
                    &m_pLightSlateGrayBrush);
            }

            if (SUCCEEDED(hr))
            {
                hr = m_pRenderTarget->CreateSolidColorBrush(
                    D2D1::ColorF(D2D1::ColorF::CornflowerBlue),
                    &m_pCornflowerBlueBrush);
            }
        }

        return hr;
    }

}
