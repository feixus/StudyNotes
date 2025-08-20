#include "stdafx.h"
#include "Image.h"
#include "Core/Paths.h"

#define STBI_NO_GIF
#define STBI_NO_PIC
#define STBI_NO_PNM

#define STB_IMAGE_IMPLEMENTATION
#include "External/stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "External/stb/stb_image_write.h"


bool Image::Load(const char* filePath)
{
	const std::string extension = Paths::GetFileExtension(filePath);
	bool success = false;
	if (extension == ".dds")
	{
		success = LoadDds(filePath);
	}
	// png, jpg, jpeg, bmp, tga
	else
	{
		success = LoadStbi(filePath);
	}
	
	return success;
}

bool Image::Load(const void* pInPixels, size_t dataSize, const char* pFormatHint)
{
	m_Components = 4;
	m_Depth = 1;
	int components = 0;

	unsigned char* pPixels = stbi_load_from_memory((stbi_uc*)pInPixels, (int)dataSize, &m_Width, &m_Height, &components, m_Components);
	if (pPixels == nullptr)
	{
		return false;
	}

	m_BBP = sizeof(uint8_t) * 8 * m_Components;
	m_Format = ImageFormat::RGBA;
	m_Pixels.resize(m_Width * m_Height * m_Components);
	memcpy(m_Pixels.data(), pPixels, m_Pixels.size());
	stbi_image_free(pPixels);
	return true;
}

bool Image::SetSize(const int x, const int y, const int components)
{
	m_Width = x;
	m_Height = y;
	m_Components = components;
	m_Depth = 1;
	m_Pixels.clear();
	m_Pixels.resize(m_Width * m_Height * m_Components);
	
	return true;
}

bool Image::SetData(const unsigned int* pPixels)
{
	memcpy(m_Pixels.data(), pPixels, m_Pixels.size() * m_Depth * m_Components);
	return true;
}

bool Image::SetPixel(const int x, const int y, const Color& color)
{
	if (x + y * m_Width >= m_Pixels.size())
	{
		return false;
	}

	uint8_t* pPixel = &m_Pixels[(x + y * m_Width) * m_Depth * m_Components];
	for (int i = 0; i < m_Components; ++i)
	{
		pPixel[i] = static_cast<uint8_t>(color[i] * 255);
	}
	return true;
}

bool Image::SetPixelInt(const int x, const int y, const unsigned int color)
{
	if (x + y * m_Width >= m_Pixels.size())
	{
		return false;
	}

	uint8_t* pPixel = &m_Pixels[(x + y * m_Width) * m_Depth * m_Components];
	for (int i = 0; i < m_Components; ++i)
	{
		pPixel[i] = reinterpret_cast<const uint8_t*>(&color)[i];
	}
	return true;
}

Color Image::GetPixel(const int x, const int y) const
{
	if (x + y * m_Width >= m_Pixels.size())
	{
		return Color();
	}

	Color color;
	const uint8_t* pPixel = &m_Pixels[(x + y * m_Width) * m_Depth * m_Components];
	for (int i = 0; i < m_Components; ++i)
	{
		reinterpret_cast<float*>(&color)[i] = (float)pPixel[i] / 255.0f;
	}
	return color;
}

unsigned int Image::GetPixelInt(const int x, const int y) const
{
	unsigned int color = 0;
	if (x + y * m_Width >= m_Pixels.size())
	{
		return color;
	}

	const uint8_t* pPixel = &m_Pixels[(x + y * m_Width) * m_Depth * m_Components];
	for (int i = 0; i < m_Components; ++i)
	{
		color |= pPixel[i] << (i * 8);
	}
	color >>= 8 * (4 - m_Components);
	return color;
}

const uint8_t* Image::GetData(int mipLevel) const
{
	if (mipLevel >= m_MipLevels)
	{
		return nullptr;
	}

	uint32_t offset = (mipLevel == 0) ? 0 : m_MipLevelDataOffsets[mipLevel];
	return m_Pixels.data() + offset;
}

MipLevelInfo Image::GetMipLevelInfo(int mipLevel) const
{
	if (mipLevel >= m_MipLevels)
	{
		return MipLevelInfo();
	}

	MipLevelInfo mipLevelInfo;
	GetSurfaceInfo(m_Width, m_Height, m_Depth, mipLevel, mipLevelInfo);
	return mipLevelInfo;
}

bool Image::GetSurfaceInfo(int width, int height, int depth, int mipLevel, MipLevelInfo& mipLevelInfo) const
{
	if (mipLevel >= m_MipLevels)
	{
		return false;
	}

	mipLevelInfo.Width = std::max<int>(1, width >> mipLevel);
	mipLevelInfo.Height = std::max<int>(1, height >> mipLevel);
	mipLevelInfo.Depth = std::max<int>(1, depth >> mipLevel);

	if (m_Format == ImageFormat::RGBA || m_Format == ImageFormat::BGRA)
	{
		mipLevelInfo.RowSize = mipLevelInfo.Width * m_BBP / 8;
		mipLevelInfo.Rows = mipLevelInfo.Height;
		mipLevelInfo.DataSize = mipLevelInfo.RowSize * mipLevelInfo.Rows * mipLevelInfo.Depth;
	}
	else if (IsCompressed())
	{
		int blockSize = (m_Format == ImageFormat::BC1 || m_Format == ImageFormat::BC4) ? 8 : 16;
		mipLevelInfo.RowSize = ((mipLevelInfo.Width + 3) / 4) * blockSize;
		mipLevelInfo.Rows = ((mipLevelInfo.Height + 3) / 4);
		mipLevelInfo.DataSize = mipLevelInfo.RowSize * mipLevelInfo.Rows * mipLevelInfo.Depth;
	}
	else
	{
		return false;
	}

	return true;
}

unsigned int Image::TextureFormatFromCompressionFormat(const ImageFormat& format, bool sRgb)
{
	switch (format)
 	{
 	case ImageFormat::RGBA: return sRgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
 	case ImageFormat::BGRA: return sRgb ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB : DXGI_FORMAT_B8G8R8A8_UNORM;
 	case ImageFormat::RGB32: return DXGI_FORMAT_R32G32B32_FLOAT;
 	case ImageFormat::RGBA16: return DXGI_FORMAT_R16G16B16A16_FLOAT;
 	case ImageFormat::RGBA32: return DXGI_FORMAT_R32G32B32A32_FLOAT;
 	case ImageFormat::BC1: return sRgb ? DXGI_FORMAT_BC1_UNORM_SRGB : DXGI_FORMAT_BC1_UNORM;
 	case ImageFormat::BC2: return sRgb ? DXGI_FORMAT_BC2_UNORM_SRGB : DXGI_FORMAT_BC2_UNORM;
 	case ImageFormat::BC3: return sRgb ? DXGI_FORMAT_BC3_UNORM_SRGB : DXGI_FORMAT_BC3_UNORM;
 	case ImageFormat::BC4: return DXGI_FORMAT_BC4_UNORM;
 	case ImageFormat::BC5: return DXGI_FORMAT_BC5_UNORM;
 	case ImageFormat::BC6H: return DXGI_FORMAT_BC6H_UF16;
 	case ImageFormat::BC7: return sRgb ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM;
 	default:
 		return DXGI_FORMAT_UNKNOWN;
 	}
}

//  +-----------------------+
//  | 4 bytes: 'DDS '       | // Magic number
//  +-----------------------+
//  | DDS_HEADER (124 bytes)| // Contains size, width, height, etc.
//  +-----------------------+
//  | Optional DDS_HEADER_BC10 (20 bytes) |
//  +-----------------------+
//  | Image Data            |
//  +-----------------------+
bool Image::LoadDds(const char* inputStream)
{
	std::ifstream file(inputStream, std::ios::binary);

	// .DDS subheader
#pragma pack(push, 1)
	struct PixelFormatHeader
	{
		uint32_t Size;
		uint32_t Flags;
		uint32_t FourCC;
		uint32_t RGBBitCount;
		uint32_t RBitMask;
		uint32_t GBitMask;
		uint32_t BBitMask;
		uint32_t ABitMask;
	};
#pragma pack(pop)

	// .DDS header
#pragma pack(push, 1)
	struct FileHeader
	{
		uint32_t Size;
		uint32_t Flags;
		uint32_t Height;
		uint32_t Width;
		uint32_t PitchOrLinearSize;
		uint32_t Depth;
		uint32_t MipMapCount;
		uint32_t Reserved1[11];
		PixelFormatHeader PixelFormat;
		uint32_t Caps;
		uint32_t Caps2;
		uint32_t Caps3;
		uint32_t Caps4;
		uint32_t Reserved2;
	};
#pragma pack(pop)

#pragma pack(push, 1)
	struct DX10FileHeader
	{
		uint32_t DXGIFormat;
		uint32_t ResourceDimension;
		uint32_t MiscFlag;
		uint32_t ArraySize;
		uint32_t Reserved;
	};
#pragma pack(pop)

	enum IMAGE_FORMAT
	{
		R8G8B8A8_UNORM = 28,
		R8G8B8A8_UNORM_SRGB = 26,
		BC1_UNORM = 71,
		BC1_UNORM_SRGB = 72,
		BC2_UNORM = 74,
		BC2_UNORM_SRGB = 75,
		BC3_UNORM = 77,
		BC3_UNORM_SRGB = 78,
		BC4_UNORM = 80,
		BC5_UNORM = 83,
		DXGI_FORMAT_BC6H_UF16 = 95,
		DXGI_FORMAT_BC7_UNORM = 98,
		DXGI_FORMAT_BC7_UNORM_SRGB = 99,
	};

	enum DDC_CAP_ATTRIBUTE
	{
		DDSCAPS_COMPLEX = 0x00000008U,
		DDSCAPS_TEXTURE = 0x00001000U,
		DDSCAPS_MIPMAP = 0x00400000U,
		DDXCAPS_VOLUME = 0x00200000U,
		DDSCAPS_CUBEMAP = 0x00000200U,
	};

#ifndef MAKEFOURCC
#define MAKEFOURCC(ch0, ch1, ch2, ch3) \
	(uint32_t)((uint8_t)(ch0) | (uint8_t)(ch1) << 8 | (uint8_t)(ch2) << 16 | (uint8_t)(ch3) << 24)
#endif

	char magic[5];
	magic[4] = '\0';
	file.read(&magic[0], 4);
	if (strcmp(magic, "DDS ") != 0)
	{
		return false;
	}
	
	FileHeader header;
	file.read(reinterpret_cast<char*>(&header), sizeof(FileHeader));

	if (header.Size == sizeof(FileHeader) && header.PixelFormat.Size == sizeof(PixelFormatHeader))
	{
		m_BBP = header.PixelFormat.RGBBitCount;

		uint32_t fourCC = header.PixelFormat.FourCC;
		char fourCCStr[5];
		fourCCStr[4] = '\0';
		memcpy(fourCCStr, &fourCC, 4);
		bool hasDxgi = fourCC == MAKEFOURCC('D', 'X', '1', '0');
		DX10FileHeader* pDx10Header = nullptr;
		if (hasDxgi)
		{
			DX10FileHeader dx10Header{};
			pDx10Header = &dx10Header;
			file.read(reinterpret_cast<char*>(&dx10Header), sizeof(DX10FileHeader));

			switch (dx10Header.DXGIFormat)
			{
			case IMAGE_FORMAT::BC1_UNORM_SRGB:
				m_Components = 3;
				m_sRgb = true;
			case IMAGE_FORMAT::BC1_UNORM:
				m_Format = ImageFormat::BC1;
				break;
			case IMAGE_FORMAT::BC2_UNORM_SRGB:
				m_Components = 4;
				m_sRgb = true;
			case IMAGE_FORMAT::BC2_UNORM:
				m_Format = ImageFormat::BC2;
				break;
			case IMAGE_FORMAT::BC3_UNORM_SRGB:
				m_Components = 4;
				m_sRgb = true;
			case IMAGE_FORMAT::BC3_UNORM:
				m_Format = ImageFormat::BC3;
				break;
			case IMAGE_FORMAT::BC4_UNORM:
				m_Components = 1;
				m_Format = ImageFormat::BC4;
				break;
			case IMAGE_FORMAT::BC5_UNORM:
				m_Components = 4;
				m_Format = ImageFormat::BC5;
				break;
			case IMAGE_FORMAT::DXGI_FORMAT_BC6H_UF16:
				m_Components = 3;
				m_Format = ImageFormat::BC6H;
				break;
			case IMAGE_FORMAT::DXGI_FORMAT_BC7_UNORM_SRGB:
				m_Components = 4;
				m_sRgb = true;
			case IMAGE_FORMAT::DXGI_FORMAT_BC7_UNORM:
				m_Format = ImageFormat::BC7;
				break;
			case IMAGE_FORMAT::R8G8B8A8_UNORM_SRGB:
				m_Components = 4;
				m_sRgb = true;
			case IMAGE_FORMAT::R8G8B8A8_UNORM:
				m_Format = ImageFormat::RGBA;
				break;
			default:
				return false;
			}
		}
		else
		{
			switch (fourCC)
			{
			case MAKEFOURCC('D', 'X', 'T', '1'):
				m_Format = ImageFormat::BC1;
				m_Components = 3;
				m_sRgb = false;
				break;
			case MAKEFOURCC('D', 'X', 'T', '3'):
				m_Format = ImageFormat::BC2;
				m_Components = 4;
				m_sRgb = false;
				break;
			case MAKEFOURCC('D', 'X', 'T', '5'):
				m_Format = ImageFormat::BC3;
				m_Components = 4;
				m_sRgb = false;
				break;
			case MAKEFOURCC('B', 'C', '5', 'U'):
			case MAKEFOURCC('A', 'T', 'I', '2'):
				m_Format = ImageFormat::BC5;
				m_Components = 2;
				m_sRgb = false;
				break;
			case 0:
				if (m_BBP == 32)
				{
					m_Components = 4;

#define ISBITMASK(r, g, b, a) \
	(header.PixelFormat.RBitMask == (r) && header.PixelFormat.GBitMask == (g) && header.PixelFormat.BBitMask == (b) && header.PixelFormat.ABitMask == (a))

					if (ISBITMASK(0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000))
					{
						m_Format = ImageFormat::RGBA;
					}
					else if (ISBITMASK(0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000))
					{
						m_Format = ImageFormat::BGRA;
					}
					else
					{
						return false;
					}
				}
#undef ISBITMASK
#undef MAKEFOURCC
				break;
			default:
				return false;
			}
		}

		bool isCubemap = (header.Caps2 & 0x0000FC00U) != 0 || (hasDxgi && (pDx10Header->MiscFlag & 0x4) != 0);
		uint32_t imageChainCount = 1;
		if (isCubemap)
		{
			imageChainCount = 6;
			m_IsCubemap = true;
		}
		else if (hasDxgi && pDx10Header->ArraySize > 1)
		{
			imageChainCount = pDx10Header->ArraySize;
			m_IsArray = true;
		}

		uint32_t totalDataSize = 0;
		m_MipLevels = std::max<int>(1, header.MipMapCount);
		for (int mipLevel = 0; mipLevel < m_MipLevels; ++mipLevel)
		{
			MipLevelInfo mipLevelInfo;
			GetSurfaceInfo(header.Width, header.Height, header.Depth, mipLevel, mipLevelInfo);
			m_MipLevelDataOffsets[mipLevel] = totalDataSize;
			totalDataSize += mipLevelInfo.DataSize;
		}

		Image* pCurrentImage = this;
		for (uint32_t imageIdx = 0; imageIdx < imageChainCount; ++imageIdx)
		{
			pCurrentImage->m_Pixels.resize(totalDataSize);
			pCurrentImage->m_Width = header.Width;
			pCurrentImage->m_Height = header.Height;
			pCurrentImage->m_Depth = header.Depth;
			pCurrentImage->m_Format = m_Format;
			pCurrentImage->m_BBP = m_BBP;
			file.read(reinterpret_cast<char*>(pCurrentImage->m_Pixels.data()), totalDataSize);
			
			if (imageIdx < imageChainCount - 1)
			{
				pCurrentImage->m_pNextImage = std::make_unique<Image>();
				pCurrentImage = pCurrentImage->m_pNextImage.get();
			}
		}
	}
	else
	{
		return false;
	}

	return true;
}

bool Image::LoadStbi(const char* inputStream)
{
	m_Components = 4;
	m_Depth = 1;
	int components = 0;

	m_IsHdr = stbi_is_hdr(inputStream);
	if (m_IsHdr)
	{
		float* pPixels = stbi_loadf(inputStream, &m_Width, &m_Height, &components, m_Components);
		if (pPixels == nullptr)
		{
			return false;
		}

		m_BBP = sizeof(float) * 8 * m_Components;
		m_Format = ImageFormat::RGBA32;
		m_Pixels.resize(m_Width * m_Height * m_Components * sizeof(float));
		memcpy(m_Pixels.data(), pPixels, m_Pixels.size());
		stbi_image_free(pPixels);
		return true;
	}
	else
	{
		unsigned char* pPixels = stbi_load(inputStream, &m_Width, &m_Height, &components, m_Components);
		if (pPixels == nullptr)
		{
			return false;
		}

		m_BBP = sizeof(uint8_t) * 8 * m_Components;
		m_Format = ImageFormat::RGBA;
		m_Pixels.resize(m_Width * m_Height * m_Components);
		memcpy(m_Pixels.data(), pPixels, m_Pixels.size());
		stbi_image_free(pPixels);
		return true;
	}
}
