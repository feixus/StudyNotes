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

struct ClearBinding
{
	struct DepthStencilData
	{
		float Depth;
		uint8_t Stencil;
	};

	enum class ClearBindingValue
	{
		None,
		Color,
		DepthStencil,
	};

	ClearBinding() : BindingValue(ClearBindingValue::None) {}
	ClearBinding(const Color& color) : BindingValue(ClearBindingValue::Color), Color(color) {}
	ClearBinding(float depth, uint8_t stencil = 0) : BindingValue(ClearBindingValue::DepthStencil)
	{
		DepthStencil.Depth = depth;
		DepthStencil.Stencil = stencil;
	}

	ClearBindingValue BindingValue;
	union
	{
		Color Color;
		DepthStencilData DepthStencil;
	};
};

class GraphicsTexture : public GraphicsResource
{
public:
	GraphicsTexture();

	int GetWidth() const { return m_Width; }
	int GetHeight() const { return m_Height; }
	int GetDepth() const { return m_Height; }
	int GetArraySize() const { return m_DepthOrArraySize; }
	int GetMipLevels() const { return m_MipLevels; }
	bool IsArray() const { return m_IsArray; }
	TextureDimension GetDimension() const { return m_Dimension; }
	DXGI_FORMAT GetFormat() const { return m_Format; }
	const ClearBinding& GetClearBinding() const { return m_ClearBinding; }

	D3D12_CPU_DESCRIPTOR_HANDLE GetRTV(int subResource = 0) const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetUAV(int subResource = 0) const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetSRV(int subResource = 0) const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetDSV(int subResource = 0) const;

	static int GetRowDataSize(DXGI_FORMAT format, unsigned int width);
	static DXGI_FORMAT GetSrvFormatFromDepth(DXGI_FORMAT format);

protected:
	void Create_Internal(Graphics* pGraphics, TextureDimension dimension, int width, int height, int depthOrArraySize, DXGI_FORMAT format, TextureUsage usage, int sampleCount, const ClearBinding& clearBinding);

	// this can hold multiple handles as long as they're sequential in memory.
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_Rtv{D3D12_DEFAULT};
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_Uav{D3D12_DEFAULT};
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_Srv{D3D12_DEFAULT};
	DXGI_FORMAT m_Format{};
	ClearBinding m_ClearBinding{};

	TextureDimension m_Dimension{};
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
	void Create(Graphics* pGraphics, CommandContext* pContext, const char* filePath, TextureUsage usage);
	void Create(Graphics* pGraphics, int width, int height, DXGI_FORMAT format, TextureUsage usage, int sampleCount, int arraySize = -1, ClearBinding clearBinding = ClearBinding());
	void SetData(CommandContext* pContext, const void* pData);
	void CreateForSwapChain(Graphics* pGraphics, ID3D12Resource* pTexture);
};

class GraphicsTexture3D : public GraphicsTexture
{
public:
	void Create(Graphics* pGraphics, int width, int height, int depth, DXGI_FORMAT format, TextureUsage usage);
};

class GraphicsTextureCube : public GraphicsTexture
{
public:
	void Create(Graphics* pGraphics, CommandContext* pContext, const char* pFilePath, TextureUsage usage);
	void Create(Graphics* pGraphics, int width, int height, DXGI_FORMAT format, TextureUsage usage, int sampleCount, int arraySize = -1);
};
