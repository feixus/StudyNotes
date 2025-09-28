#pragma once
#include <iostream>
#include <fstream>
#include "ImageParser.hpp"

namespace My
{
#pragma pack(push, 1)
    typedef struct _BITMAP_FILEHEADER {
        uint16_t Signature;
        uint32_t Size;
        uint32_t Reserved;
        uint32_t BitsOffset;
    } BITMAP_FILEHEADER;

#define BITMAP_FILEHEADER_SIZE 14

    typedef struct _BITMAP_HEADER {
        uint32_t HeaderSize;
        int32_t Width;
        int32_t Height;
        uint16_t Planes;
        uint16_t BitCount;
        uint32_t Compression;
        uint32_t SizeImage;
        int32_t PelsPerMeterX;
        int32_t PelsPerMeterY;
        uint32_t ClrUsed;
        uint32_t ClrImportant;
    } BITMAP_HEADER;
    
#pragma pack(pop)

    // 32-bit, BMP rows are bottom-to-top, each row is passed to 4-byte multiples, pixels are BGR(A) order
    class BmpParser : implements ImageParser
    {
    public:
        virtual Image Parse(const Buffer& buf) override
        {
            Image img;

            const BITMAP_FILEHEADER* pFileHeader = reinterpret_cast<const BITMAP_FILEHEADER*>(buf.GetData());
            const BITMAP_HEADER* pBmpHeader = reinterpret_cast<const BITMAP_HEADER*>(buf.GetData() + BITMAP_FILEHEADER_SIZE);

            if (pFileHeader->Signature == 0x4D42 /* 'B''M'*/) {
                std::cout << "Asset is Windows BMP file" << std::endl;
                std::cout << "BMP Header" << std::endl;
                std::cout << "-------------------------------------------------" << std::endl;
                std::cout << "File Size: " << pFileHeader->Size << std::endl;
                std::cout << "Data Offset: " << pFileHeader->BitsOffset << std::endl;
                std::cout << "Image Width: " << pBmpHeader->Width << std::endl;
                std::cout << "Image Height: " << pBmpHeader->Height << std::endl;
                std::cout << "Image Planes: " << pBmpHeader->Planes << std::endl;
                std::cout << "Image BitCount: " << pBmpHeader->BitCount << std::endl;
                std::cout << "Image Compression: " << pBmpHeader->Compression << std::endl;
                std::cout << "Image Size: " << pBmpHeader->SizeImage << std::endl;

                img.width = pBmpHeader->Width;
                img.height = pBmpHeader->Height;
                img.bitcount = 32;
                auto byte_count = img.bitcount >> 3;
                img.pitch = (img.width * byte_count + 3) & ~3; // padded to a 4-byte boundary
                img.data_size = img.pitch * img.height;
                img.data = new uint8_t[img.data_size];

                if (img.bitcount < 24) {
                    std::cout << "BMP file is not supported" << std::endl;
                } else {
                     const uint8_t* pSourceData = buf.GetData() + pFileHeader->BitsOffset;
                     for (int32_t y = img.height - 1; y >= 0; y--) {
                         for (uint32_t x = 0; x < img.width; x++) {
                            auto dst = reinterpret_cast<R8G8B8A8Unorm*>(img.data + img.pitch * (img.height - y - 1) + x * byte_count);
                            auto src = reinterpret_cast<const R8G8B8A8Unorm*>(pSourceData + img.pitch * y + x * byte_count);
                            dst->data[2] = src->data[0];
							dst->data[1] = src->data[1];
							dst->data[0] = src->data[2];
							dst->data[3] = src->data[3];
                         }
                     }
                }
            }

            WriteToPPM(img);

            return img;
        }
    private:
        void WriteToPPM(const Image& img)
        {
			std::ofstream file("output.ppm", std::ios::binary);
			if (!file.is_open()) {
				std::cerr << "Error: Cannot create PPM file " << std::endl;
				return;
			}

			// Write PPM header
			file << "P6\n";
			file << img.width << " " << img.height << "\n";
			file << "255\n";

			for (int32_t y = 0; y < img.height; y++) {
				for (uint32_t x = 0; x < img.width; x++) {
					auto pixel = reinterpret_cast<R8G8B8A8Unorm*>(img.data + img.pitch * y + x * (img.bitcount >> 3));
					file.put(pixel->r);
					file.put(pixel->g);
					file.put(pixel->b);
				}
			}

			file.close();
        }
    };
}

