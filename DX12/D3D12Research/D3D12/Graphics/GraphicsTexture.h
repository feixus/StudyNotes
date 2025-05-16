#pragma once
#include "GraphicsResource.h"

class CommandContext;
class Graphics;

enum class TextureUsage
{
	UnorderedAccess = 1 << 1,
	ShaderResource = 1 << 2,
	RenderTarget = 1 << 3,
	DepthStencil = 1 << 4,
};
DEFINE_ENUM_FLAG_OPERATORS(TextureUsage)

enum class TextureDimension
{
	Texture1D,
	Texture1DArray,
	Texture2D,
	Texture2DArray,
	Texture3D,
	TextureCube,
	TextureCubeArray,
};

enum class CubeMapFace
{
	POSITIVE_X = 0,
	NEGATIVE_X,
	POSITIVE_Y,
	NEGATIVE_Y,
	POSITIVE_Z,
	NEGATIVE_Z,
	MAX
};

class GraphicsTexture : public GraphicsResource
{
public:
	GraphicsTexture(ID3D12Device* pDevice);

	int GetWidth() const { return m_Width; }
	int GetHeight() const { return m_Height; }
	int GetDepth() const { return m_Height; }
	int GetArraySize() const { return m_DepthOrArraySize; }
	int GetMipLevels() const { return m_MipLevels; }
	bool IsArray() const { return m_IsArray; }
	TextureDimension GetDimension() const { return m_Dimension; }

	D3D12_CPU_DESCRIPTOR_HANDLE GetRTV(int subResource = 0) const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetUAV(int subResource = 0) const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetSRV(int subResource = 0) const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetDSV(int subResource = 0) const;

	static int GetRowDataSize(DXGI_FORMAT format, unsigned int width);
	static DXGI_FORMAT GetSrvFormatFromDepth(DXGI_FORMAT format);

protected:
	void Create_Internal(Graphics* pGraphics, TextureDimension dimension, int width, int height, int depthOrArraySize, DXGI_FORMAT format, TextureUsage usage, int sampleCount);

	D3D12_CPU_DESCRIPTOR_HANDLE m_Rtv{};
	D3D12_CPU_DESCRIPTOR_HANDLE m_Uav{};
	D3D12_CPU_DESCRIPTOR_HANDLE m_Srv{};
	DXGI_FORMAT m_Format{};

	TextureDimension m_Dimension;
	int m_SampleCount{1};
	int m_Width{0};
	int m_Height{0};
	int m_DepthOrArraySize{0};
	int m_MipLevels{0};
	bool m_IsArray{false};

	int m_SrvUavDescriptorSize{0};
	int m_RtvDescriptorSize{0};
};

class GraphicsTexture2D : public GraphicsTexture
{
public:
	GraphicsTexture2D(ID3D12Device* pDevice);

	void Create(Graphics* pGraphics, CommandContext* pContext, const char* filePath, TextureUsage usage);
	void Create(Graphics* pGraphics, int width, int height, DXGI_FORMAT format, TextureUsage usage, int sampleCount);
	void SetData(CommandContext* pContext, const void* pData);
	void CreateForSwapChain(Graphics* pGraphics, ID3D12Resource* pTexture);
};

class GraphicsTextureCube : public GraphicsTexture
{
public:
	GraphicsTextureCube(ID3D12Device* pDevice);
	void Create(Graphics* pGraphics, int width, int height, DXGI_FORMAT format, TextureUsage usage, int sampleCount, int arraySize = -1);
};
