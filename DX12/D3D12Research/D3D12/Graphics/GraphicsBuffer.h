#pragma once
#include "GraphicsResource.h"

class CommandContext;
class Graphics;

class GraphicsBuffer : public GraphicsResource
{
public:
	GraphicsBuffer() = default;
	GraphicsBuffer(ID3D12Resource* pResource, D3D12_RESOURCE_STATES state);

	void SetData(CommandContext* pContext, const void* pData, uint64_t dataSize, uint32_t offset = 0);

	void* Map(uint32_t subResource = 0, uint64_t readFrom = 0, uint64_t readTo = 0);
	void UnMap(uint32_t subResource = 0, uint64_t writeFrom = 0, uint64_t writeTo = 0);
	
	inline uint64_t GetSize() const { return m_ElementStride * m_ElementCount; }
	inline uint32_t GetStride() const { return m_ElementStride; }
	inline uint64_t GetElementCount() const { return m_ElementCount; }

	D3D12_CPU_DESCRIPTOR_HANDLE GetSRV() const { return m_Srv; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetUAV() const { return m_Uav; }

protected:
	void Create(Graphics* pGraphics, uint64_t elementCount, uint32_t elementStride, bool cpuVisible);
	virtual void CreateViews(Graphics* pGraphics) {};

	uint32_t m_ElementStride{ 0 };
	uint64_t m_ElementCount{ 0 };

	CD3DX12_CPU_DESCRIPTOR_HANDLE m_Srv{ D3D12_DEFAULT };
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_Uav{ D3D12_DEFAULT };
};

/*
* raw buffer, can read arbitrary bytes at any offset, no predefined format.
*/
class ByteAddressBuffer : public GraphicsBuffer
{
public:
	ByteAddressBuffer(Graphics* pGraphics);

	void Create(Graphics* pGraphics, uint32_t elementStride, uint64_t elementCount, bool cpuVisible = false);
	virtual void CreateViews(Graphics* pGraphics) override;
};

class StructuredBuffer : public GraphicsBuffer
{
public:
	StructuredBuffer(Graphics* pGraphics);
	~StructuredBuffer();

	void Create(Graphics* pGraphics, uint32_t elementStride, uint64_t elementCount, bool cpuVisible = false);
	void Create(Graphics* pGraphics, uint32_t elementStride, uint64_t elementCount, ByteAddressBuffer* pCounter, uint64_t counterOffset = 0, bool cpuVisible = false);
	virtual void CreateViews(Graphics* pGraphics) override;

	ByteAddressBuffer* GetCounter() const { return m_pCounter; }

private:
	ByteAddressBuffer* m_pCounter{nullptr};
	uint32_t m_CounterBufferOffset{0};
	bool m_CounterOwner{true};
};

class TypedBuffer : public GraphicsBuffer
{
public:
	TypedBuffer(Graphics* pGraphics);
	void Create(Graphics* pGraphics, DXGI_FORMAT format, uint64_t elementCount, bool cpuVisible = false);
	static bool FormatIsUAVCompatible(ID3D12Device* pDevice, bool typedUAVLoadAdditionalFormats, DXGI_FORMAT format);
	virtual void CreateViews(Graphics* pGraphics) override;

	DXGI_FORMAT GetFormat() const { return m_Format; }

private:
	DXGI_FORMAT m_Format;
};

class VertexBuffer : public GraphicsBuffer
{
public:
	void Create(Graphics* pGraphics, uint64_t elementCount, uint32_t elementStride, bool cpuVisible = false);
	virtual void CreateViews(Graphics* pGraphics) override;

	inline const D3D12_VERTEX_BUFFER_VIEW GetView() const { return m_View; }

private:
	D3D12_VERTEX_BUFFER_VIEW m_View;
};

class IndexBuffer : public GraphicsBuffer
{
public:
	void Create(Graphics* pGraphics, bool smallIndices, uint32_t elementCount, bool cpuVisible = false);
	virtual void CreateViews(Graphics* pGraphics) override;

	inline const D3D12_INDEX_BUFFER_VIEW GetView() const { return m_View; }

private:
	bool m_SmallIndices{ false };
	D3D12_INDEX_BUFFER_VIEW m_View;
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
class ReadbackBuffer : public GraphicsBuffer
{
public:
	void Create(Graphics* pGraphics, uint64_t size);
private:
	D3D12_INDEX_BUFFER_VIEW m_View;
};