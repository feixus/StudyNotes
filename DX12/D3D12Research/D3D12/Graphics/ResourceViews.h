#pragma once

class Buffer;
class Graphics;
class GraphicsTexture;
class GraphicsResource;

struct BufferUAVDesc
{
	static BufferUAVDesc CreateStructured(Buffer* pCounter = nullptr)
	{
		BufferUAVDesc desc;
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.FirstElement = 0;
		desc.CounterOffset = 0;
		desc.pCounter = pCounter;
		return desc;
	}

	static BufferUAVDesc CreateTyped(DXGI_FORMAT format, Buffer* pCounter = nullptr)
	{
		BufferUAVDesc desc;
		desc.Format = format;
		desc.FirstElement = 0;
		desc.CounterOffset = 0;
		desc.pCounter = pCounter;
		return desc;
	}

	static BufferUAVDesc ByteAddress()
	{
		BufferUAVDesc desc;
		desc.Format = DXGI_FORMAT_R32_TYPELESS;
		desc.FirstElement = 0;
		desc.CounterOffset = 0;
		desc.pCounter = nullptr;
		return desc;
	}

	DXGI_FORMAT Format;
	int FirstElement;
	int CounterOffset;
	Buffer* pCounter;
};

struct BufferSRVDesc
{
	static BufferSRVDesc CreateStructured(Buffer* pCounter = nullptr)
	{
		BufferSRVDesc desc;
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.FirstElement = 0;
		return desc;
	}

	static BufferSRVDesc CreateTyped(DXGI_FORMAT format, Buffer* pCounter = nullptr)
	{
		BufferSRVDesc desc;
		desc.Format = format;
		desc.FirstElement = 0;
		return desc;
	}

	static BufferSRVDesc ByteAddress()
	{
		BufferSRVDesc desc;
		desc.Format = DXGI_FORMAT_R32_TYPELESS;
		desc.FirstElement = 0;
		return desc;
	}

	DXGI_FORMAT Format;
	int FirstElement;
};

struct TextureSRVDesc
{
    DXGI_FORMAT Format;
    uint8_t MipLevel;
    uint8_t NumMipLevels;
    uint32_t FirstArraySlice;
    uint32_t NumArraySlices;

    bool operator==(const TextureSRVDesc& other) const
    {
        return Format == other.Format && 
                MipLevel == other.MipLevel && 
                NumMipLevels == other.NumMipLevels && 
                FirstArraySlice == other.FirstArraySlice && 
                NumArraySlices == other.NumArraySlices;
    }

    bool operator!=(const TextureSRVDesc& other) const
    {
        return !(*this == other);
    }
};

struct TextureUAVDesc
{
    uint8_t MipLevel;

    bool operator==(const TextureUAVDesc& other) const
    {
        return MipLevel == other.MipLevel;
    }
};

class DescriptorBase
{
public:
	GraphicsResource* GetParent() const { return m_pParent; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetDescriptor() const { return m_Descriptor; }

protected:
	GraphicsResource* m_pParent{ nullptr };
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_Descriptor{ D3D12_DEFAULT };
};

class ShaderResourceView : public DescriptorBase
{
public:
	ShaderResourceView() = default;
	void Create(Graphics* pGraphics, Buffer* pBuffer, const BufferSRVDesc& desc);
	void Create(Graphics* pGraphics, GraphicsTexture* pTexture, const TextureSRVDesc& desc);
};

class UnorderedAccessView : public DescriptorBase
{
public:
	UnorderedAccessView() = default;
	void Create(Graphics* pGraphics, Buffer* pBuffer, const BufferUAVDesc& desc);
	void Create(Graphics* pGraphics, GraphicsTexture* pTexture, const TextureUAVDesc& desc);
};

