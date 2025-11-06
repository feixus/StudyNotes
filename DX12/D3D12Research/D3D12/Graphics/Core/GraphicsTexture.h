#pragma once
#include "GraphicsResource.h"

class CommandContext;
class Graphics;
class UnorderedAccessView;
class ShaderResourceView;
class ResourceView;
struct TextureUAVDesc;
struct TextureSRVDesc;
class Image;

enum class TextureFlag
{
	None = 0,
	UnorderedAccess = 1 << 1,
	ShaderResource = 1 << 2,
	RenderTarget = 1 << 3,
	DepthStencil = 1 << 4,
};
DECLARE_BITMASK_TYPE(TextureFlag)

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
		float Depth{0.f};
		uint8_t Stencil{1};
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

	bool operator==(const ClearBinding& other) const
	{
		if (BindingValue != other.BindingValue)
		{
			return false;
		}
		if (BindingValue == ClearBindingValue::Color)
		{
			return Color == other.Color;
		}
		return DepthStencil.Depth == other.DepthStencil.Depth && DepthStencil.Stencil == other.DepthStencil.Stencil;
	}

	ClearBindingValue BindingValue;
	union
	{
		Color Color;
		DepthStencilData DepthStencil{};
	};
};

struct TextureDesc
{
	TextureDesc() : Width(1), Height(1), DepthOrArraySize(1), Mips(1), SampleCount(1), Format(DXGI_FORMAT_UNKNOWN), Usage(TextureFlag::None), ClearBindingValue(), Dimension(TextureDimension::Texture2D) {}
	TextureDesc(int width, int height, DXGI_FORMAT format, TextureFlag usage = TextureFlag::ShaderResource, int sampleCount = 1, const ClearBinding& clearBinding = ClearBinding())
		: Width(width), Height(height), DepthOrArraySize(1), Mips(1), SampleCount(sampleCount), Format(format), Usage(usage), ClearBindingValue(clearBinding), Dimension(TextureDimension::Texture2D) {}
	TextureDesc(int width, int height, int depth, DXGI_FORMAT format, TextureFlag usage = TextureFlag::ShaderResource, TextureDimension dimension = TextureDimension::Texture2D)
		: Width(width), Height(height), DepthOrArraySize(depth), Mips(1), SampleCount(1), Format(format), Usage(usage), Dimension(dimension), ClearBindingValue() {}

	int Width;
	int Height;
	int DepthOrArraySize;
	int Mips;
	int SampleCount;
	DXGI_FORMAT Format;
	TextureFlag Usage;
	ClearBinding ClearBindingValue;
	TextureDimension Dimension;

	static TextureDesc Create2D(int width, int height, DXGI_FORMAT format, TextureFlag flag = TextureFlag::ShaderResource, int sampleCount = 1, int mips = 1)
	{
		check(width);
		check(height);
		TextureDesc desc{};
		desc.Width = width;
		desc.Height = height;
		desc.DepthOrArraySize = 1;
		desc.Mips = mips;
		desc.SampleCount = sampleCount;
		desc.Format = format;
		desc.Usage = flag;
		desc.ClearBindingValue = ClearBinding();
		desc.Dimension = TextureDimension::Texture2D;
		return desc;
	}

	static TextureDesc CreateDepth(int width, int height, DXGI_FORMAT format, TextureFlag flags = TextureFlag::DepthStencil, int sampleCount = 1, const ClearBinding& clearBinding = ClearBinding(1, 0))
	{
		check(width);
		check(height);
		check(Any(flags, TextureFlag::DepthStencil));
		TextureDesc desc{};
		desc.Width = width;
		desc.Height = height;
		desc.DepthOrArraySize = 1;
		desc.Mips = 1;
		desc.SampleCount = sampleCount;
		desc.Format = format;
		desc.Usage = flags;
		desc.ClearBindingValue = clearBinding;
		desc.Dimension = TextureDimension::Texture2D;
		return desc;
	}

	static TextureDesc CreateRenderTarget(int width, int height, DXGI_FORMAT format, TextureFlag flags = TextureFlag::RenderTarget, int sampleCount = 1, const ClearBinding& clearBinding = ClearBinding(Color(0, 0, 0)))
	{
		check(width);
		check(height);
		check(Any(flags, TextureFlag::RenderTarget));
		TextureDesc desc{};
		desc.Width = width;
		desc.Height = height;
		desc.DepthOrArraySize = 1;
		desc.Mips = 1;
		desc.SampleCount = sampleCount;
		desc.Format = format;
		desc.Usage = flags;
		desc.ClearBindingValue = clearBinding;
		desc.Dimension = TextureDimension::Texture2D;
		return desc;
	}

	static TextureDesc Create3D(int width, int height, int depth, DXGI_FORMAT format, TextureFlag flags = TextureFlag::ShaderResource, TextureDimension dimension = TextureDimension::Texture3D, int sampleCount = 1)
	{
		check(width);
		check(height);
		TextureDesc desc{};
		desc.Width = width;
		desc.Height = height;
		desc.DepthOrArraySize = depth;
		desc.Mips = 1;
		desc.SampleCount = sampleCount;
		desc.Format = format;
		desc.Usage = flags;
		desc.ClearBindingValue = ClearBinding();
		desc.Dimension = dimension;
		return desc;
	}

	static TextureDesc CreateCube(int width, int height, DXGI_FORMAT format, TextureFlag flags = TextureFlag::ShaderResource, int sampleCount = 1, int mips = 1)
	{
		check(width);
		check(height);
		TextureDesc desc{};
		desc.Width = width;
		desc.Height = height;
		desc.DepthOrArraySize = 1;
		desc.Mips = mips;
		desc.SampleCount = sampleCount;
		desc.Format = format;
		desc.Usage = flags;
		desc.ClearBindingValue = ClearBinding();
		desc.Dimension = TextureDimension::TextureCube;
		return desc;
	}

	bool operator==(const TextureDesc& other) const
	{
		return Width == other.Width &&
			Height == other.Height &&
			DepthOrArraySize == other.DepthOrArraySize &&
			Mips == other.Mips &&
			SampleCount == other.SampleCount &&
			Format == other.Format &&
			Usage == other.Usage &&
			ClearBindingValue == other.ClearBindingValue &&
			Dimension == other.Dimension;
	}

	bool operator !=(const TextureDesc& other) const
	{
		return !operator==(other);
	}
};

class GraphicsTexture : public GraphicsResource
{
public:
	GraphicsTexture(Graphics* pGraphics, const char* pName = "");
	~GraphicsTexture();

	int GetWidth() const { return m_Desc.Width; }
	int GetHeight() const { return m_Desc.Height; }
	int GetDepth() const { return m_Desc.DepthOrArraySize; }
	int GetArraySize() const { return m_Desc.DepthOrArraySize; }
	int GetMipLevels() const { return m_Desc.Mips; }
	const TextureDesc& GetDesc() const { return m_Desc; }

	void Create(const TextureDesc& desc);
	bool Create(CommandContext* pContext, const char* pFilePath, bool srgb = false);
	bool Create(CommandContext* pContext, const Image& image, bool srgb = false);
	void Create(CommandContext* pContext, const TextureDesc& desc, void* pInitialData);
	void CreateForSwapChain(ID3D12Resource* pTexture);
	void SetData(CommandContext* pContext, const void* pData);

	void CreateUAV(UnorderedAccessView** pView, const TextureUAVDesc& desc);
	void CreateSRV(ShaderResourceView** pView, const TextureSRVDesc& desc);

	D3D12_CPU_DESCRIPTOR_HANDLE GetRTV() const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetUAV() const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetSRV() const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetDSV(bool writeable = true) const;

	DXGI_FORMAT GetFormat() const { return m_Desc.Format; }
	const ClearBinding& GetClearBinding() const { return m_Desc.ClearBindingValue; }

	static DXGI_FORMAT GetSrvFormat(DXGI_FORMAT format);

private:
	TextureDesc m_Desc;

	ShaderResourceView* m_pSrv{ nullptr };
	UnorderedAccessView* m_pUav{ nullptr };

	CD3DX12_CPU_DESCRIPTOR_HANDLE m_Rtv{D3D12_DEFAULT};
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_ReadOnlyDsv{D3D12_DEFAULT};

	std::string m_Name{nullptr};
};
