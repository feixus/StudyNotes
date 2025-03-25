#include <iostream>
#include "geommath.hpp"

using namespace std;
using namespace My;

void vector_test()
{
    Vector2f x = { 55.3f, 33.1f };
    cout << "Vector2f: ";
    cout << x;
}

int main()
{
    cout << std::fixed;

    vector_test();

    return 0;
}

/*
clang++ -I"../ispc" -I"../include"  -o test.exe test.cpp CrossProduct.o
*/