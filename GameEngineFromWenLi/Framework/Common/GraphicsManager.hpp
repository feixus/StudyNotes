#pragma once

#include "IRuntimeModule.hpp"

namespace My
{
    class GraphicsManager : public IRuntimeModule
    {
    public:
        virtual ~GraphicsManager() {}

        virtual int Initialize();
        virtual void Finalize();

        virtual void Tick();
    };
}