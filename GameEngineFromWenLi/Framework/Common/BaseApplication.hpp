#pragma once

#include "IApplication.hpp"
#include "GfxConfiguration.h"

namespace My 
{
    class BaseApplication : implements IApplication 
    {
    public:
        BaseApplication(GfxConfiguration& cfg);
        BaseApplication() = delete;

        virtual int Initialize();
        virtual void Finalize();

        // One cycle of the main loop
        virtual void Tick();

        virtual bool IsQuit();

    protected:
        // Flag if the program is quit
        static bool m_bQuit;

        GfxConfiguration m_Config;;
    };
}
