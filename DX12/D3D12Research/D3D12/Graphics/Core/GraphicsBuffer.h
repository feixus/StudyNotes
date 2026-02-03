#pragma once
#include "GraphicsResource.h"

class CommandContext;
class GraphicsDevice;
class GraphicsBuffer;
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
	BufferDesc(uint32_t elements, uint32_t elementSize, BufferFlag usage = BufferFlag::None)
		: Size((uint64_t)elements * elementSize), ElementSize(elementSize), Usage(usage) {}

	static BufferDesc CreateBuffer(uint64_t sizeInBytes, BufferFlag usage = BufferFlag::None)
	{
		BufferDesc desc;
		desc.Size = sizeInBytes;
		desc.ElementSize = 1;
		desc.Usage = usage;
		return desc;
	}

	static BufferDesc CreateIndexBuffer(uint32_t elements, bool smallIndices, BufferFlag usage = BufferFlag::None)
	{
		return BufferDesc(elements, smallIndices ? 2 : 4, usage);
	}

	static BufferDesc CreateVertexBuffer(uint32_t elements, uint32_t vertexSize, BufferFlag usage = BufferFlag::None)
	{
		return BufferDesc(elements, vertexSize, usage);
	}

	static BufferDesc CreateReadback(uint64_t size)
	{
		return CreateBuffer(size, BufferFlag::Readback);
	}

	static BufferDesc CreateStructured(uint32_t elementCount, uint32_t elementSize, BufferFlag usage = BufferFlag::ShaderResource | BufferFlag::UnorderedAccess)
	{
		BufferDesc desc;
		desc.ElementSize = elementSize;
		desc.Size = (uint64_t)elementCount * desc.ElementSize;
		desc.Usage = usage | BufferFlag::Structured;
		return desc;
	}

	static BufferDesc CreateByteAddress(uint64_t bytes, BufferFlag usage = BufferFlag::ShaderResource)
	{
		check(bytes % 4 == 0);
		BufferDesc desc;
		desc.Size = bytes;
		desc.ElementSize = 4;
		desc.Usage = usage | BufferFlag::ByteAddress | BufferFlag::UnorderedAccess;
		return desc;
	}

	template<typename IndirectParameters>
	static BufferDesc CreateIndirectArgumemnts(uint32_t elements = 1, BufferFlag usage = BufferFlag::None)
	{
		BufferDesc desc;
		desc.ElementSize = sizeof(IndirectParameters);
		desc.Size = (uint64_t)elements * desc.ElementSize;
		desc.Usage = usage | BufferFlag::IndirectArgument | BufferFlag::UnorderedAccess;
		return desc;
	}

	static BufferDesc CreateAccelerationStructure(uint64_t bytes)
	{
		check(bytes % 4 == 0);
		BufferDesc desc;
		desc.Size = bytes;
		desc.ElementSize = 4;
		desc.Usage = desc.Usage | BufferFlag::AccelerationStructure | BufferFlag::UnorderedAccess;
		return desc;
	}

	static BufferDesc CreateTyped(uint32_t elementCount, DXGI_FORMAT format, BufferFlag usage = BufferFlag::ShaderResource | BufferFlag::UnorderedAccess)
	{
		check(!D3D::IsBlockCompressFormat(format));
		BufferDesc desc;
		desc.ElementSize = D3D::GetFormatRowDataSize(format, 1);
		desc.Size = (uint64_t)elementCount * desc.ElementSize;
		desc.Usage = usage;
		desc.Format = format;
		return desc;
	}

	uint32_t NumElements() const
	{
		return static_cast<uint32_t>(Size / ElementSize);
	}

	bool operator==(const BufferDesc& other) const
	{
		return Size == other.Size && ElementSize == other.ElementSize && Usage == other.Usage;
	}

	bool operator!=(const BufferDesc& other) const
	{
		return !(*this == other);
	}

	uint64_t Size{0};
	uint32_t ElementSize{0};
	BufferFlag Usage{BufferFlag::None};
	DXGI_FORMAT Format{DXGI_FORMAT_UNKNOWN};
};

class GraphicsBuffer : public GraphicsResource
{
public:
	GraphicsBuffer(GraphicsDevice* pGraphics, const char* pName = "");
	GraphicsBuffer(GraphicsDevice* pGraphics, const BufferDesc& desc, const char* pName = "");
	GraphicsBuffer(GraphicsDevice* pGraphics, ID3D12Resource* pResource, D3D12_RESOURCE_STATES state);
	~GraphicsBuffer();

	void Create(const BufferDesc& desc);
	void SetData(CommandContext* pContext, const void* pData, uint64_t dataSize, uint64_t offset = 0);

	inline uint64_t GetSize() const { return m_Desc.Size; }
	inline uint32_t GetNumElements() const { return m_Desc.NumElements(); }
	inline const BufferDesc& GetDesc() const { return m_Desc; }

	void CreateUAV(UnorderedAccessView** pView, const BufferUAVDesc& desc);
	void CreateSRV(ShaderResourceView** pView, const BufferSRVDesc& desc);

	ShaderResourceView* GetSRV() const { return m_pSrv; }
	UnorderedAccessView* GetUAV() const { return m_pUav; }

	GraphicsBuffer* GetCounter() const { return m_pCounter.get(); }

protected:
	UnorderedAccessView* m_pUav{ nullptr };
	ShaderResourceView* m_pSrv{ nullptr };
	std::unique_ptr<GraphicsBuffer> m_pCounter;

	BufferDesc m_Desc;
};

struct VertexBufferView
{
	VertexBufferView() : Location(~0u), Elements(0), Stride(0)
	{}

	VertexBufferView(D3D12_GPU_VIRTUAL_ADDRESS location, uint32_t elements, uint32_t stride)
		: Location(location), Elements(elements), Stride(stride)
	{}

	VertexBufferView(GraphicsBuffer* pBuffer)
	{
		Location = pBuffer->GetGpuHandle();
		Stride = pBuffer->GetDesc().ElementSize;
		Elements = (uint32_t)(pBuffer->GetSize() / Stride);
	}

	D3D12_GPU_VIRTUAL_ADDRESS Location;
	uint32_t Elements;
	uint32_t Stride;
};

struct IndexBufferView
{
	IndexBufferView() : Location(~0u), Elements(0), Format(DXGI_FORMAT_R32_UINT)
	{}

	IndexBufferView(D3D12_GPU_VIRTUAL_ADDRESS location, uint32_t elements, DXGI_FORMAT format = DXGI_FORMAT_R32_UINT)
		: Location(location), Elements(elements), Format(format)
	{}

	uint32_t Stride() const
	{
		return D3D::GetFormatRowDataSize(Format, 1);
	}

	D3D12_GPU_VIRTUAL_ADDRESS Location;
	uint32_t Elements;
	bool SmallIndices;
	DXGI_FORMAT Format;
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
