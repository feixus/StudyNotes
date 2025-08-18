#pragma once

#include <filesystem>
#include <fstream>
#include <vector>
#include <cstddef>
#include <stdexcept>
#include <string>

#define HR(hr) \
D3D::LogHRESULT(hr)

#ifdef _DEBUG
#define D3D_SETNAME(obj, name) D3D::SetD3DObjectName(obj, name)
#else
#define D3D_SETNAME(obj, name)
#endif

/**
 * @brief Reads a file into a byte vector
 * @param filePath Path to the file to read
 * @param mode File open mode (default: std::ios::ate for reading from end)
 * @return std::vector<std::byte> containing the file contents
 * @throws std::runtime_error if file cannot be opened or read
 */
static std::vector<std::byte> ReadFile(const std::filesystem::path& filePath, std::ios_base::openmode mode = std::ios::ate)
{
	if (filePath.empty())
	{
		throw std::runtime_error("File path is empty");
	}

	if (!std::filesystem::exists(filePath))
	{
		throw std::runtime_error("File does not exist: " + filePath.string());
	}

	std::ifstream file(filePath, mode | std::ios::binary);
	if (!file)
	{
		throw std::runtime_error("Failed to open file: " + filePath.string());
	}

	const auto size = static_cast<size_t>(file.tellg());
	if (size == 0)
	{
		return {};
	}

	std::vector<std::byte> buffer(size);
	file.seekg(0);
	
	if (!file.read(reinterpret_cast<char*>(buffer.data()), size))
	{
		throw std::runtime_error("Failed to read file: " + filePath.string());
	}

	file.close();
	return buffer;
}

inline int ToMultibyte(const wchar_t* pStr, char* pOut, int len)
{
	return WideCharToMultiByte(CP_UTF8, 0, pStr, -1, pOut, len, nullptr, nullptr);
}

inline int ToWidechar(const char* pStr, wchar_t* pOut, int len)
{
	return MultiByteToWideChar(CP_UTF8, 0, pStr, -1, pOut, len);
}



namespace D3D
{
	inline bool LogHRESULT(HRESULT hr)
	{
		if (SUCCEEDED(hr))
		{
			return true;
		}

		char* errorMsg;
		if (FormatMessageA(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
			nullptr, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPSTR)&errorMsg, 0, nullptr) != 0)
		{
			E_LOG(LogType::Error, "Error: %s", errorMsg);
		}

		__debugbreak();
		return false;
	}

	inline void SetD3DObjectName(ID3D12Object* pObject, const char* pName)
	{
		if (pObject)
		{
			pObject->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)strlen(pName), pName);
		}
	}

	inline int GetRowDataSize(DXGI_FORMAT format, unsigned int width)
	{
		switch (format)
		{
		case DXGI_FORMAT_R8_UNORM:
		case DXGI_FORMAT_A8_UNORM:
		case DXGI_FORMAT_R8_UINT:
			return (unsigned)width;

		case DXGI_FORMAT_R8G8_UNORM:
		case DXGI_FORMAT_R16_UNORM:
		case DXGI_FORMAT_R16_FLOAT:
		case DXGI_FORMAT_R16_TYPELESS:
		case DXGI_FORMAT_R16_UINT:
			return (unsigned)(width * 2);

		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_R16G16_UNORM:
		case DXGI_FORMAT_R16G16_FLOAT:
		case DXGI_FORMAT_R32_FLOAT:
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_R32_TYPELESS:
		case DXGI_FORMAT_R32_UINT:
			return (unsigned)(width * 4);

		case DXGI_FORMAT_R16G16B16A16_UNORM:
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
			return (unsigned)(width * 8);

		case DXGI_FORMAT_R32G32B32A32_FLOAT:
			return (unsigned)(width * 16);

		case DXGI_FORMAT_BC1_TYPELESS:
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC1_UNORM_SRGB:
		case DXGI_FORMAT_BC4_TYPELESS:
		case DXGI_FORMAT_BC4_UNORM:
		case DXGI_FORMAT_BC4_SNORM:
			return (unsigned)(((width + 3) >> 2) * 8);

		case DXGI_FORMAT_BC2_TYPELESS:
		case DXGI_FORMAT_BC2_UNORM:
		case DXGI_FORMAT_BC2_UNORM_SRGB:
		case DXGI_FORMAT_BC3_TYPELESS:
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC3_UNORM_SRGB:
		case DXGI_FORMAT_BC5_TYPELESS:
		case DXGI_FORMAT_BC5_UNORM:
		case DXGI_FORMAT_BC5_SNORM:
		case DXGI_FORMAT_BC6H_TYPELESS:
		case DXGI_FORMAT_BC6H_UF16:
		case DXGI_FORMAT_BC6H_SF16:
		case DXGI_FORMAT_BC7_TYPELESS:
		case DXGI_FORMAT_BC7_UNORM:
		case DXGI_FORMAT_BC7_UNORM_SRGB:
			return (unsigned)(((width + 3) >> 2) * 16);
		case DXGI_FORMAT_R32G32B32_FLOAT:
			return width * 3 * sizeof(float);
		default:
			assert(false);
			return 0;
		}
	}

	inline DXGI_FORMAT GetSrvFormatFromDepth(DXGI_FORMAT format)
	{
		switch (format)
		{
			// 32-bit Z w/ Stencil
		case DXGI_FORMAT_R32G8X24_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
		case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
			return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;

			// No Stencil
		case DXGI_FORMAT_R32_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_R32_FLOAT:
			return DXGI_FORMAT_R32_FLOAT;

			// 24-bit Z
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
			return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;

			// 16-bit Z w/o Stencil
		case DXGI_FORMAT_R16_TYPELESS:
		case DXGI_FORMAT_D16_UNORM:
		case DXGI_FORMAT_R16_UNORM:
			return DXGI_FORMAT_R16_UNORM;

		default:
			return DXGI_FORMAT_UNKNOWN;
		}
	}

	inline DXGI_FORMAT GetDsvFormat(DXGI_FORMAT format)
	{
		switch (format)
		{
		case DXGI_FORMAT_R32_TYPELESS:
		case DXGI_FORMAT_R32_FLOAT:
			return DXGI_FORMAT_D32_FLOAT;
		case DXGI_FORMAT_R16_TYPELESS:
		case DXGI_FORMAT_R16_FLOAT:
			return DXGI_FORMAT_D16_UNORM;
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
			return DXGI_FORMAT_D24_UNORM_S8_UINT;
		case DXGI_FORMAT_R32G8X24_TYPELESS:
			return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
		}
		return format;
	}

	inline bool HasStencil(DXGI_FORMAT format)
	{
		return format == DXGI_FORMAT_D24_UNORM_S8_UINT || format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
	}
}


