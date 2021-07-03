#include "engine.h"

int main()
{
    Engine engine(1136, 640, "OpenGL preset by Guylian Gilsing");

    if(!engine.Initialize())
    {
        std::cout << std::endl << "Press any key to close program..." << std::endl;
        std::cin.get();
    }
}

