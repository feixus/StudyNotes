#pragma once
#include "GraphicsResource.h"

class CommandContext;
class Graphics;
class Buffer;
class ShaderResourceView;
class UnorderedAccessView;
class ResourceView;
struct BufferSRVDesc;
struct BufferUAVDesc;

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
	AccelerationStructure = 1 << 7,

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

	static BufferDesc CreateByteAddress(uint64_t bytes, BufferFlag usage = BufferFlag::ShaderResource)
	{
		assert(bytes % 4 == 0);
		BufferDesc desc;
		desc.ElementCount = (uint32_t)bytes / 4;
		desc.ElementSize = 4;
		desc.Usage = usage | BufferFlag::ByteAddress | BufferFlag::UnorderedAccess;
		return desc;
	}

	template<typename IndirectParameters>
	static BufferDesc CreateIndirectArgumemnts(int elements = 1, BufferFlag usage = BufferFlag::None)
	{
		BufferDesc desc;
		desc.ElementCount = elements;
		desc.ElementSize = sizeof(IndirectParameters);
		desc.Usage = usage | BufferFlag::IndirectArgument | BufferFlag::UnorderedAccess;
		return desc;
	}

	static BufferDesc CreateAccelerationStructure(uint64_t bytes, BufferFlag usage = BufferFlag::None)
	{
		assert(bytes % 4 == 0);
		BufferDesc desc;
		desc.ElementCount = (uint32_t)bytes / 4;
		desc.ElementSize = 4;
		desc.Usage = usage | BufferFlag::AccelerationStructure | BufferFlag::UnorderedAccess;
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

class Buffer : public GraphicsResource
{
public:
	Buffer(Graphics* pGraphics, const char* pName = "");
	Buffer(Graphics* pGraphics, ID3D12Resource* pResource, D3D12_RESOURCE_STATES state);
	~Buffer();

	void Create(const BufferDesc& desc);
	void SetData(CommandContext* pContext, const void* pData, uint64_t dataSize, uint32_t offset = 0);

	void* Map(uint32_t subResource = 0, uint64_t readFrom = 0, uint64_t readTo = 0);
	void UnMap(uint32_t subResource = 0, uint64_t writeFrom = 0, uint64_t writeTo = 0);

	inline uint64_t GetSize() const { return (uint64_t)m_Desc.ElementCount * m_Desc.ElementSize; }
	const BufferDesc& GetDesc() const { return m_Desc; }

	void CreateUAV(UnorderedAccessView** pView, const BufferUAVDesc& desc);
	void CreateSRV(ShaderResourceView** pView, const BufferSRVDesc& desc);

	ShaderResourceView* GetSRV() const { return m_pSrv; }
	UnorderedAccessView* GetUAV() const { return m_pUav; }

	Buffer* GetCounter() const { return m_pCounter.get(); }

protected:
	UnorderedAccessView* m_pUav{ nullptr };
	ShaderResourceView* m_pSrv{ nullptr };
	std::unique_ptr<Buffer> m_pCounter;

	std::string m_Name;
	BufferDesc m_Desc;
	std::vector<std::unique_ptr<ResourceView>> m_Descriptors;
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