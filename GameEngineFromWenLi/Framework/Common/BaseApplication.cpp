#include "BaseApplication.hpp"

bool My::BaseApplication::m_bQuit = false;

My::BaseApplication::BaseApplication(GfxConfiguration &cfg)
    :m_Config(cfg)
{
}

int My::BaseApplication::Initialize()
{
    std::wcout << m_Config;

    return 0;
}

void My::BaseApplication::Finalize() {

}

void My::BaseApplication::Tick() {

}

bool My::BaseApplication::IsQuit() {
    return m_bQuit;
}

