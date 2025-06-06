#include "stdafx.h"
#include "GraphicsBuffer.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "GraphicsTexture.h"

GraphicsBuffer::GraphicsBuffer(ID3D12Resource* pResource, D3D12_RESOURCE_STATES state)
	: GraphicsResource(pResource, state)
{
}

void GraphicsBuffer::Create(Graphics* pGraphics, uint64_t elementCount, uint32_t elementStride, bool cpuVisible)
{
	Release();

	m_ElementCount = elementCount;
	m_ElementStride = elementStride;

	m_CurrentState = cpuVisible ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COMMON;
	
	D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(GetSize(), D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
	D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(cpuVisible ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT);
	HR(pGraphics->GetDevice()->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		m_CurrentState,
		nullptr,
		IID_PPV_ARGS(&m_pResource)));

	CreateViews(pGraphics);
}

void GraphicsBuffer::SetData(CommandContext* pContext, const void* pData, uint64_t dataSize, uint32_t offset)
{
	assert(dataSize + offset <= GetSize());
	pContext->InitializeBuffer(this, pData, dataSize);
}

void* GraphicsBuffer::Map(uint32_t subResource /*= 0*/, uint64_t readFrom /*= 0*/, uint64_t readTo /*= 0*/)
{
	assert(m_pResource);

	CD3DX12_RANGE range(readFrom, readTo);
	void* pMappedData = nullptr;
	m_pResource->Map(subResource, &range, &pMappedData);
	return pMappedData;
}

void GraphicsBuffer::UnMap(uint32_t subResource /*= 0*/, uint64_t writeFrom /*= 0*/, uint64_t writeTo /*= 0*/)
{
	assert(m_pResource);

	CD3DX12_RANGE range(writeFrom, writeTo);
	m_pResource->Unmap(subResource, &range);
}

StructuredBuffer::StructuredBuffer(Graphics* pGraphics)
{
	m_Uav = pGraphics->AllocateCpuDescriptors(1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_Srv = pGraphics->AllocateCpuDescriptors(1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

StructuredBuffer::~StructuredBuffer()
{
	// delete the counter if we own it
	if (m_pCounter && m_CounterOwner)
	{
		delete m_pCounter;
		m_pCounter = nullptr;
	}
}

void StructuredBuffer::Create(Graphics* pGraphics, uint32_t elementStride, uint64_t elementCount, bool cpuVisible)
{
	Release();

	m_ElementCount = elementCount;
	m_ElementStride = elementStride;

	m_CurrentState = cpuVisible ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COMMON;

	D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(GetSize(), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	m_pResource = pGraphics->CreateResource(desc, m_CurrentState, cpuVisible ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT);
	CreateViews(pGraphics);
}

void StructuredBuffer::Create(Graphics* pGraphics, uint32_t elementStride, uint64_t elementCount, ByteAddressBuffer* pCounter, uint64_t counterOffset, bool cpuVisible)
{
	m_pCounter = pCounter;
	m_CounterBufferOffset = counterOffset * D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT;
	m_CounterOwner = false;
	Create(pGraphics, elementStride, elementCount, cpuVisible);
}

void StructuredBuffer::CreateViews(Graphics* pGraphics)
{
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Buffer.CounterOffsetInBytes = m_CounterBufferOffset;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	uavDesc.Buffer.NumElements = (uint32_t)m_ElementCount;
	uavDesc.Buffer.StructureByteStride = m_ElementStride;
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;

	// structured buffer with counters
	if (m_CounterOwner && m_pCounter == nullptr)
	{
		m_pCounter = new ByteAddressBuffer(pGraphics);
		m_pCounter->Create(pGraphics, 4, 1, false);
	}

	pGraphics->GetDevice()->CreateUnorderedAccessView(m_pResource, m_pCounter->GetResource(), &uavDesc, m_Uav);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = (uint32_t)m_ElementCount;
	srvDesc.Buffer.StructureByteStride = m_ElementStride;
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	pGraphics->GetDevice()->CreateShaderResourceView(m_pResource, &srvDesc, m_Srv);
}

ByteAddressBuffer::ByteAddressBuffer(Graphics* pGraphics)
{
	m_Uav = pGraphics->AllocateCpuDescriptors(1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_Srv = pGraphics->AllocateCpuDescriptors(1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void ByteAddressBuffer::Create(Graphics* pGraphics, uint32_t elementStride, uint64_t elementCount, bool cpuVisible /*= false*/)
{
	m_ElementCount = elementCount;
	m_ElementStride = elementStride;
	m_CurrentState = cpuVisible ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COMMON;

	D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(GetSize(), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	m_pResource = pGraphics->CreateResource(desc, m_CurrentState, cpuVisible ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT);

	CreateViews(pGraphics);
}

void ByteAddressBuffer::CreateViews(Graphics* pGraphics)
{
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Buffer.CounterOffsetInBytes = 0;
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
	uavDesc.Buffer.NumElements = (uint32_t)m_ElementCount;
	uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;

	pGraphics->GetDevice()->CreateUnorderedAccessView(m_pResource, nullptr, &uavDesc, m_Uav);
	
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.NumElements = (uint32_t)m_ElementCount;
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;

	pGraphics->GetDevice()->CreateShaderResourceView(m_pResource, &srvDesc, m_Srv);
}

void VertexBuffer::Create(Graphics* pGraphics, uint64_t elementCount, uint32_t elementStride, bool cpuVisible)
{
	GraphicsBuffer::Create(pGraphics, elementCount, elementStride, cpuVisible);
}

void VertexBuffer::CreateViews(Graphics* pGraphics)
{
	m_View.BufferLocation = GetGpuHandle();
	m_View.SizeInBytes = (uint32_t)GetSize();
	m_View.StrideInBytes = GetStride();
}

void IndexBuffer::Create(Graphics* pGraphics, bool smallIndices, uint32_t elementCount, bool cpuVisible /*= false*/)
{
	m_SmallIndices = smallIndices;
	GraphicsBuffer::Create(pGraphics, smallIndices ? 2 : 4, elementCount, cpuVisible);
}

void IndexBuffer::CreateViews(Graphics* pGraphics)
{
	m_View.BufferLocation = GetGpuHandle();
	m_View.SizeInBytes = (uint32_t)GetSize();
	m_View.Format = m_SmallIndices ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
}

void ReadbackBuffer::Create(Graphics* pGraphics, uint64_t size)
{
	m_ElementCount = size;
	m_ElementStride = 1;

	m_CurrentState = D3D12_RESOURCE_STATE_COPY_DEST;

	D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(GetSize(), D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
	m_pResource = pGraphics->CreateResource(desc, m_CurrentState, D3D12_HEAP_TYPE_READBACK);
}

TypedBuffer::TypedBuffer(Graphics* pGraphics)
{
	m_Uav = pGraphics->AllocateCpuDescriptors(1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_Srv = pGraphics->AllocateCpuDescriptors(1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void TypedBuffer::Create(Graphics* pGraphics, DXGI_FORMAT format, uint64_t elementCount, bool cpuVisible)
{
	Release();

	// can load from UAV with more DXGI formats
	assert(pGraphics->CheckTypedUAVSupport(format));

	m_ElementCount = elementCount;
	m_ElementStride = GraphicsTexture::GetRowDataSize(format, 1);
	m_CurrentState = cpuVisible ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COMMON;
	m_Format = format;

	D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(GetSize(), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	m_pResource = pGraphics->CreateResource(desc, m_CurrentState, cpuVisible ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT);

	CreateViews(pGraphics);
}

void TypedBuffer::CreateViews(Graphics* pGraphics)
{
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Buffer.CounterOffsetInBytes = 0;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	uavDesc.Buffer.NumElements = (uint32_t)m_ElementCount;
	uavDesc.Format = m_Format;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;

	pGraphics->GetDevice()->CreateUnorderedAccessView(m_pResource, nullptr, &uavDesc, m_Uav);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.NumElements = (uint32_t)m_ElementCount;
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.StructureByteStride = m_ElementStride;

	pGraphics->GetDevice()->CreateShaderResourceView(m_pResource, &srvDesc, m_Srv);
}
