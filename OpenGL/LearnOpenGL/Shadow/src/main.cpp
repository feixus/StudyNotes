#include "engine.h"

int main()
{
    Engine engine(1136, 640, "Learn OpenGL PBR");

    if(!engine.Initialize())
    {
        std::cout << std::endl << "Press any key to close program..." << std::endl;
        std::cin.get();
    }
}

