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
GeomMath.lib(Normalize.o) : error LNK2019: unresolved external symbol DotProduct___un_3C_unf_3E_un_3C_unf_3E_un_3C_unf_
3E_uni referenced in function Normalize___un_3C_unf_3E_Cuni

dumpbin(Windows)/nm(Linux/Unix) to locate where the mangled symbol is defined or referenced.
such as: dumpbin /symbols Normalize.o | findstr DotProduct
此处是Normalize.o引用的DotProduct的参数不一致引起的


*/