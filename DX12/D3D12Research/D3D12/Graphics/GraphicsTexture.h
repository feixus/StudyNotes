#pragma once
#include "GraphicsResource.h"

class CommandContext;
class Graphics;

enum class TextureUsage
{
	None = 0,
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
		float Depth{0};
		uint8_t Stencil{0};
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

struct TextureDesc
{
	TextureDesc() : Width(1), Height(1), DepthOrArraySize(1), Mips(1), SampleCount(1), Format(DXGI_FORMAT_UNKNOWN), Usage(TextureUsage::None), ClearBindingValue(), Dimension(TextureDimension::Texture2D) {}
	TextureDesc(int width, int height, DXGI_FORMAT format, TextureUsage usage = TextureUsage::ShaderResource, int sampleCount = 1, const ClearBinding& clearBinding = ClearBinding())
		: Width(width), Height(height), DepthOrArraySize(1), Mips(1), SampleCount(sampleCount), Format(format), Usage(usage), ClearBindingValue(clearBinding), Dimension(TextureDimension::Texture2D) {}
	TextureDesc(int width, int height, int depth, DXGI_FORMAT format, TextureUsage usage = TextureUsage::ShaderResource, TextureDimension dimension = TextureDimension::Texture2D)
		: Width(width), Height(height), DepthOrArraySize(depth), Mips(1), SampleCount(1), Format(format), Usage(usage), Dimension(dimension), ClearBindingValue() {}

	int Width;
	int Height;
	int DepthOrArraySize;
	int Mips;
	int SampleCount;
	DXGI_FORMAT Format;
	TextureUsage Usage;
	ClearBinding ClearBindingValue;
	TextureDimension Dimension;
};

class GraphicsTexture : public GraphicsResource
{
public:
	using Descriptor = TextureDesc;

	GraphicsTexture();

	int GetWidth() const { return m_Desc.Width; }
	int GetHeight() const { return m_Desc.Height; }
	int GetDepth() const { return m_Desc.DepthOrArraySize; }
	int GetArraySize() const { return m_Desc.DepthOrArraySize; }
	int GetMipLevels() const { return m_Desc.Mips; }
	const TextureDesc& GetDesc() const { return m_Desc; }

	void Create(Graphics* pGraphics, const Descriptor& desc);
	void Create(Graphics* pGraphics, CommandContext* pContext, const char* pFilePath);
	void CreateForSwapChain(Graphics* pGraphics, ID3D12Resource* pTexture);
	void SetData(CommandContext* pContext, const void* pData);

	D3D12_CPU_DESCRIPTOR_HANDLE GetRTV(int subResource = 0) const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetUAV(int subResource = 0) const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetSRV(int subResource = 0) const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetDSV(bool writeable = true) const;

	DXGI_FORMAT GetFormat() const { return m_Desc.Format; }
	const ClearBinding& GetClearBinding() const { return m_Desc.ClearBindingValue; }

	static int GetRowDataSize(DXGI_FORMAT format, unsigned int width);
	static DXGI_FORMAT GetSrvFormatFromDepth(DXGI_FORMAT format);

private:
	void Create_Internal(Graphics* pGraphics, TextureDimension dimension, int width, int height, int depthOrArraySize, DXGI_FORMAT format, TextureUsage usage, int sampleCount, const ClearBinding& clearBinding);

	// this can hold multiple handles as long as they're sequential in memory.
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_Rtv{D3D12_DEFAULT};
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_Uav{D3D12_DEFAULT};
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_Srv{D3D12_DEFAULT};

	Descriptor m_Desc;

	int m_SrvUavDescriptorSize{0};
	int m_RtvDescriptorSize{0};
	int m_DsvDescriptorSize{0};
};