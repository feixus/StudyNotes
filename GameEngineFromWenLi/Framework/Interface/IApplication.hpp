# pragma once

#include "Interface.hpp"
#include "IRuntimeModule.hpp"

namespace My {
    Interface IApplication : implements IRuntimeModule {
    public:
        virtual int Initialize() = 0;
        virtual void Finalize() = 0;

        // One cycle of the main loop
        virtual void Tick() = 0;

        virtual bool IsQuit() = 0;
    };
}
