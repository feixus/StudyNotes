#include <iostream>
#include "geommath.hpp"

using namespace std;
using namespace My;

void vector_test()
{
    Vector2f x = { 55.3f, 33.1f };
    cout << "Vector2f: ";
    cout << x;

    Vector3f a = { 1.0f, 2.0f, 3.0f };
    Vector3f b = { 5.0f, 6.0f, 7.0f };
    cout << "Vector3f vec 1: ";
    cout << a;
    cout << "Vector3f vec 2: ";
    cout << b;

    Vector3f c;
    CrossProduct(c, a, b);
    cout << "Crosss Product of vec1 and vec2: ";
    cout << c;

    float d;
    DotProduct(d, a, b);
    cout << "Dot Product of vec1 and vec2: ";
    cout << d << std::endl;

    MulByElement(c, a, b);
    cout << "MulByElement of vec1 and vec2: ";
    cout << c;

    Vector4f e = { 1.0f, 2.0f, 3.0f, 4.0f };
    Vector4f f = { 5.0f, 6.0f, 7.0f, 8.0f };
    cout << "Vec 3: " << e;
    cout << "vec 4: " << f;

    Vector4f g = e + f;
    cout << "vec3 + vec4 = " << g;
    g = e - f;
    cout << "vec3 - vec4 = " << g;

    Normalize(g);
    cout << "Normalize: " << g << std::endl;
}

void matrix_test()
{
    Matrix4X4f ml;
    BuildIdentityMatrix(ml);

    cout << "Identity Matrix: " << ml;

    float yaw = 0.2f, pitch = 0.3f, roll = 0.4f;
    MatrixRotationYawPitchRoll(ml, yaw, pitch, roll);
    
    cout << "Matrix of yaw(" << yaw << "), pitch(" << pitch << "), roll(" << roll << "): " << ml;

    Matrix4X4f rx;
    float angle = PI / 2.0f;
    MatrixRotationX(rx, angle);

    cout << "Matrix of rotation on X(angle = " << angle << "): " << rx;

    Matrix4X4f ry;
    MatrixRotationY(ry, angle);

    cout << "Matrix of rotation on Y(angle = " << angle << "): " << ry;

    Matrix4X4f rz;
    MatrixRotationZ(rz, angle);

    cout << "Matrix of rotation on Z(angle = " << angle << "): " << rz;

    float x = 5.0f, y = 6.5f, z = -7.0f;
    Matrix4X4f translate;
    MatrixTranslation(translate, x, y, z);

    cout << "Matrix of translation on X(" << x << ") Y(" << y << ") Z(" << z << "): " << translate;

    Vector3f v = { 1.0f, 0.0f, 0.0f };

    Vector3f v1 = v;
    cout << "Vector : " << v1;
    cout << "Transform by Rotation Y Matrix:";
    cout << ry;
    TransformCoord(v1, ry);
    cout << "Nor the vector become: " << v1 << std::endl;

    v1 = v;
    cout << "Vector : " << v1;
    cout << "Transform by Rotation Z Matrix:";
    cout << rz;
    TransformCoord(v1, rz);
    cout << "Now the vector becomes: " << v1;
    cout << std::endl;

    v1 = v;
    cout << "Vector : " << v1;
    cout << "Transform by Translation Matrix:";
    cout << translate;
    TransformCoord(v1, translate);
    cout << "Now the vector becomes: " << v1 << std::endl;

    Vector3f position = { 0, 0, -5 }, lookAt = { 0, 0, 0 }, up = { 0, 1, 0 };
    Matrix4X4f view;
    BuildViewMatrix(view, position, lookAt, up);
    cout << "View matrix with position(" << position << ") lookAt(" << lookAt << ") up(" << up << "): " << view;

    float fov = PI / 2.0f, aspect = 16.0f / 9.0f, near = 1.0f, far = 100.0f;
    Matrix4X4f perspective;
    BuildPerspectiveFovLHMatrix(perspective, fov, aspect, near, far);
    cout << "Perspective matrix with fov(" << fov << ") aspect(" << aspect << ") near ... far (" << near << " ... " << far << "): " << perspective;

    Matrix4X4f mvp = view * perspective;
    cout << "MVP: " << mvp;

}

int main()
{
    cout << std::fixed;

    vector_test();

    matrix_test();

    return 0;
}

/*
GeomMath.lib(Normalize.o) : error LNK2019: unresolved external symbol DotProduct___un_3C_unf_3E_un_3C_unf_3E_un_3C_unf_
3E_uni referenced in function Normalize___un_3C_unf_3E_Cuni

dumpbin(Windows)/nm(Linux/Unix) to locate where the mangled symbol is defined or referenced.
such as: dumpbin /symbols Normalize.o | findstr DotProduct
此处是Normalize.o引用的DotProduct的函数签名不一致引起的


*/