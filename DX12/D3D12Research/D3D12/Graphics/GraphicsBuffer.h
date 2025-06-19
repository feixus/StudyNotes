#pragma once
#include "GraphicsResource.h"

class CommandContext;
class Graphics;
class Buffer;

enum class BufferFlag
{
	None = 0,
	UnorderedAccess = 1 << 0,
	ShaderResource = 1 << 1,
	Upload = 1 << 2,
	Readback = 1 << 3,
	Structured = 1 << 4,
	ByteAddress = 1 << 5,
	IndirectArgument = 1 << 6,

	MAX = 1 << 7
};
DECLARE_BITMASK_TYPE(BufferFlag)

struct BufferDesc
{
	BufferDesc() = default;
	BufferDesc(uint32_t elementCount, uint32_t stride, BufferFlag usage = BufferFlag::None)
		: ElementCount(elementCount), ElementSize(stride), Usage(usage) {}

	static BufferDesc CreateIndexBuffer(uint32_t elements, bool smallIndices, BufferFlag usage = BufferFlag::None)
	{
		return BufferDesc(elements, smallIndices ? 2 : 4, usage);
	}

	static BufferDesc CreateVertexBuffer(uint32_t elements, int vertexSize, BufferFlag usage = BufferFlag::None)
	{
		return BufferDesc(elements, vertexSize, usage);
	}

	static BufferDesc CreateReadback(uint32_t size)
	{
		return BufferDesc(size, sizeof(uint64_t), BufferFlag::Readback);
	}

	static BufferDesc CreateStructured(uint32_t elementCount, uint32_t elementSize, BufferFlag usage = BufferFlag::ShaderResource | BufferFlag::UnorderedAccess)
	{
		BufferDesc desc;
		desc.ElementCount = elementCount;
		desc.ElementSize = elementSize;
		desc.Usage = usage | BufferFlag::Structured;
		return desc;
	}

	static BufferDesc CreateByteAddress(uint32_t bytes, BufferFlag usage = BufferFlag::ShaderResource | BufferFlag::UnorderedAccess)
	{
		assert(bytes % 4 == 0);
		BufferDesc desc;
		desc.ElementCount = bytes / 4;
		desc.ElementSize = 4;
		desc.Usage = usage | BufferFlag::ByteAddress;
		return desc;
	}

	template<typename IndirectParameters>
	static BufferDesc CreateIndirectArgumemnts(int elements = 1, BufferFlag usage = BufferFlag::IndirectArgument | BufferFlag::UnorderedAccess)
	{
		BufferDesc desc;
		desc.ElementCount = elements;
		desc.ElementSize = sizeof(IndirectParameters);
		desc.Usage = usage | BufferFlag::IndirectArgument;
		return desc;
	}

	bool operator==(const BufferDesc& other) const
	{
		return ElementCount == other.ElementCount && ElementSize == other.ElementSize && Usage == other.Usage;
	}

	bool operator!=(const BufferDesc& other) const
	{
		return !(*this == other);
	}

	uint32_t ElementCount{ 0 };
	uint32_t ElementSize{ 0 };
	BufferFlag Usage{ BufferFlag::None };
};

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

class Buffer : public GraphicsResource
{
public:
	Buffer() = default;
	Buffer(ID3D12Resource* pResource, D3D12_RESOURCE_STATES state);
	void Create(Graphics* pGraphics, const BufferDesc& desc);
	void SetData(CommandContext* pContext, const void* pData, uint64_t dataSize, uint32_t offset = 0);

	void* Map(uint32_t subResource = 0, uint64_t readFrom = 0, uint64_t readTo = 0);
	void UnMap(uint32_t subResource = 0, uint64_t writeFrom = 0, uint64_t writeTo = 0);

	inline uint64_t GetSize() const { return m_Desc.ElementCount * m_Desc.ElementSize; }
	const BufferDesc& GetDesc() const { return m_Desc; }

protected:
	BufferDesc m_Desc;
};

class DescriptorBase
{
public:
	Buffer* GetParent() const { return m_pParent; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetDescriptor() const { return m_Descriptor; }

protected:
	Buffer* m_pParent{ nullptr };
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_Descriptor{ D3D12_DEFAULT };
};

class BufferSRV : public DescriptorBase
{
public:
	BufferSRV() = default;
	void Create(Graphics* pGraphics, Buffer* pBuffer, const BufferSRVDesc& desc);
};

class BufferUAV : public DescriptorBase
{
public:
	BufferUAV() = default;
	void Create(Graphics* pGraphics, Buffer* pBuffer, const BufferUAVDesc& desc);
};

class BufferWithDescriptor : public Buffer
{
public:
	D3D12_CPU_DESCRIPTOR_HANDLE GetSRV() const { return m_Srv; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetUAV() const { return m_Uav; }

protected:
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_Uav{};
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_Srv{};
};

/*
* raw buffer, can read arbitrary bytes at any offset, no predefined format.
*/
class ByteAddressBuffer : public BufferWithDescriptor
{
public:
	ByteAddressBuffer(Graphics* pGraphics);

	void Create(Graphics* pGraphics, uint32_t elementStride, uint32_t elementCount, bool cpuVisible = false);
	void CreateViews(Graphics* pGraphics);
};

class StructuredBuffer : public BufferWithDescriptor
{
public:
	StructuredBuffer(Graphics* pGraphics);

	void Create(Graphics* pGraphics, uint32_t elementStride, uint32_t elementCount, bool cpuVisible = false);
	void CreateViews(Graphics* pGraphics);

	ByteAddressBuffer* GetCounter() const { return m_pCounter.get(); }

private:
	std::unique_ptr<ByteAddressBuffer> m_pCounter;
};


 /*
 * normal readback buffer
	first create a buffer with D3D12_HEAP_TYPE_READBUFFER.
	copy data from GPU buffer to readback buffer via CopyResource/CopyBufferRegion
	execute and wait for GPU to finish
	Map and access data on CPU
 
 * readback GPU timestamp
	- first create a query heap of type TIMESTAMP
	- then create a readback buffer with D3D12_HEAP_TYPE_READBACK
	- then insert timestamp queries in command lists with EndQuery
	- then resolve both query data to the readback buffer with ResolveQueryData
	- execute and wait for GPU to finish
	- Map the readback buffer and compute time difference
*/