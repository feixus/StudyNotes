#include "engine.h"

int main()
{
    Engine engine(1134, 750, "Learn OpenGL Scene Graph");

    if(!engine.Initialize())
    {
        std::cout << std::endl << "Press any key to close program..." << std::endl;
        std::cin.get();
    }
}

