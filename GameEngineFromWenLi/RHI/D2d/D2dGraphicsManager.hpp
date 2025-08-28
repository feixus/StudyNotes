
#pragma once
#include <stdint.h>
#include <d2d1.h>
#include "GraphicsManager.hpp"


namespace My
{
    class D2dGraphicsManager : public GraphicsManager
    {
    public:
        virtual int Initialize();
        virtual void Finalize();

        virtual void Tick();

        void DrawRect();
    private:
        HRESULT CreateGraphicsResources();
        void DiscardGraphicsResources();

    private:
        
        ID2D1Factory             *m_pFactory{};
        ID2D1HwndRenderTarget    *m_pRenderTarget{};
        ID2D1SolidColorBrush     *m_pLightSlateGrayBrush{};
        ID2D1SolidColorBrush     *m_pCornflowerBlueBrush{};
    };
}