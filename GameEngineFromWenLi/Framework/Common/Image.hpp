#pragma once
#include "geommath.hpp"

namespace My 
{
    typedef struct _Image {
        uint32_t width;
        uint32_t height;
        R8G8B8A8Unorm* data;
        uint32_t bitcount;      // 一个像素在内存上占据的尺寸(bit)
        uint32_t pitch;         // 一行像素在内存上占据的尺寸(byte)
        size_t data_size;       // 整个图像在内存上占据的尺寸(byte): pitch * height, 内存区域的对齐问题, 贴图每行的数据尺寸若不满足内存对齐的要求则在行尾会有padding
    } Image;
}
