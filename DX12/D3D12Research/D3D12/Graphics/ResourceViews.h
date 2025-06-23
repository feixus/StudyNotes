#pragma once
#include "GraphicsResource.h"

class Buffer;
class Graphics;
class GraphicsTexture;
class GraphicsResource;

struct BufferUAVDesc
{
	BufferUAVDesc(DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN, bool raw = false, bool counter = false)
		: Format(format), Raw(raw), Counter(counter) {}

	DXGI_FORMAT Format;
	bool Raw;
	bool Counter;
};

struct BufferSRVDesc
{
	BufferSRVDesc(DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN, bool raw = false)
        : Format(format), Raw(raw) {}

	DXGI_FORMAT Format;
	bool Raw;
};

struct TextureSRVDesc
{
    TextureSRVDesc(uint8_t mipLevel = 0) : MipLevel(mipLevel) {}

    uint8_t MipLevel;

    bool operator==(const TextureSRVDesc& other) const
    {
        return MipLevel == other.MipLevel;
    }

    bool operator!=(const TextureSRVDesc& other) const
    {
        return !(*this == other);
    }
};

struct TextureUAVDesc
{
    explicit TextureUAVDesc(uint8_t mipLevel = 0) : MipLevel(mipLevel) {}

    uint8_t MipLevel;

    bool operator==(const TextureUAVDesc& other) const
    {
        return MipLevel == other.MipLevel;
    }
};

class ResourceView : GraphicsObject
{
public:
	ResourceView(Graphics* pGraphics);
    virtual ~ResourceView() = default;

	GraphicsResource* GetParent() const { return m_pParent; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetDescriptor() const { return m_Descriptor; }

protected:
	GraphicsResource* m_pParent{ nullptr };
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_Descriptor{ D3D12_DEFAULT };
};

class ShaderResourceView : public ResourceView
{
public:
	ShaderResourceView(Graphics* pGraphics);
	~ShaderResourceView();

	void Create(Buffer* pBuffer, const BufferSRVDesc& desc);
	void Create(GraphicsTexture* pTexture, const TextureSRVDesc& desc);
	void Release();
};

class UnorderedAccessView : public ResourceView
{
public:
	UnorderedAccessView(Graphics* pGraphics);
	~UnorderedAccessView();

	void Create(Buffer* pBuffer, const BufferUAVDesc& desc);
	void Create(GraphicsTexture* pTexture, const TextureUAVDesc& desc);
	void Release();

    Buffer* GetCounter() const { return m_pCounter.get(); }
    UnorderedAccessView* GetCounterUAV() const;
    ShaderResourceView* GetCounterSRV() const;

private:
    std::unique_ptr<Buffer> m_pCounter;
};

