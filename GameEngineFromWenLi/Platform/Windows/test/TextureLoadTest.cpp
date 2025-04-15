#include "WindowsApplication.hpp"
#include "D2d/D2dGraphicsManager.hpp"
#include "MemoryManager.hpp"
#include <iostream> 

namespace My
{
    class TestD2dApplication : public My::WindowsApplication
    {
    public:
        using My::WindowsApplication::WindowsApplication;

        virtual void OnDraw() override;
    };

    GfxConfiguration config(8, 8, 8, 8, 32, 0, 0, 1024, 512, L"TestD2dApplication");
    IApplication* g_pApp = static_cast<IApplication*>(new TestD2dApplication(config));
    GraphicsManager* g_pGraphicsManager = static_cast<GraphicsManager*>(new D2dGraphicsManager());
    MemoryManager* g_pMemoryManager = static_cast<MemoryManager*>(new MemoryManager());
}

void My::TestD2dApplication::OnDraw()
{
    dynamic_cast<D2dGraphicsManager*>(g_pGraphicsManager)->DrawRect();
}
