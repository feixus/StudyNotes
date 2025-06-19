#include "stdafx.h"
#include "GraphicsBuffer.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "GraphicsTexture.h"

Buffer::Buffer(ID3D12Resource* pResource, D3D12_RESOURCE_STATES state)
	: GraphicsResource(pResource, state)
{}

void Buffer::Create(Graphics* pGraphics, const BufferDesc& bufferDesc)
{
	Release();
	m_Desc = bufferDesc;

	D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer((int64_t)bufferDesc.ElementSize * bufferDesc.ElementCount);

	D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT;
	if (Any(bufferDesc.Usage, BufferFlag::ShaderResource) == false)
	{
		desc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
	}

	if (Any(bufferDesc.Usage, BufferFlag::UnorderedAccess))
	{
		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	}

	if (Any(bufferDesc.Usage, BufferFlag::Readback))
	{
		m_CurrentState = D3D12_RESOURCE_STATE_COPY_DEST;
		heapType = D3D12_HEAP_TYPE_READBACK;
	}
	else if (Any(bufferDesc.Usage, BufferFlag::Upload))
	{
		m_CurrentState = D3D12_RESOURCE_STATE_GENERIC_READ;
		heapType = D3D12_HEAP_TYPE_UPLOAD;
	}

	m_pResource = pGraphics->CreateResource(desc, m_CurrentState, heapType);
}

void Buffer::SetData(CommandContext* pContext, const void* pData, uint64_t dataSize, uint32_t offset /*= 0*/)
{
	assert(dataSize + offset <= GetSize());
	pContext->InitializeBuffer(this, pData, dataSize);
}

void* Buffer::Map(uint32_t subResource /*= 0*/, uint64_t readFrom /*= 0*/, uint64_t readTo /*= 0*/)
{
	assert(m_pResource);

	CD3DX12_RANGE range(readFrom, readTo);
	void* pMappedData = nullptr;
	m_pResource->Map(subResource, &range, &pMappedData);
	return pMappedData;
}

void Buffer::UnMap(uint32_t subResource /*= 0*/, uint64_t writeFrom /*= 0*/, uint64_t writeTo /*= 0*/)
{
	assert(m_pResource);

	CD3DX12_RANGE range(writeFrom, writeTo);
	m_pResource->Unmap(subResource, &range);
}

ByteAddressBuffer::ByteAddressBuffer(Graphics* pGraphics)
{
	m_Uav = pGraphics->AllocateCpuDescriptors(1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_Srv = pGraphics->AllocateCpuDescriptors(1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void ByteAddressBuffer::Create(Graphics* pGraphics, uint32_t elementStride, uint32_t elementCount, bool cpuVisible /*= false*/)
{
	BufferFlag flags = BufferFlag::ShaderResource | BufferFlag::UnorderedAccess;
	if (cpuVisible)
	{
		flags |= BufferFlag::Upload;
	}
	Buffer::Create(pGraphics, BufferDesc::CreateByteAddress(elementCount * elementStride, flags))	;
	CreateViews(pGraphics);
}

void ByteAddressBuffer::CreateViews(Graphics* pGraphics)
{
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Buffer.CounterOffsetInBytes = 0;
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
	uavDesc.Buffer.NumElements = (uint32_t)m_Desc.ElementCount;
	uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;

	pGraphics->GetDevice()->CreateUnorderedAccessView(m_pResource, nullptr, &uavDesc, m_Uav);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.NumElements = (uint32_t)m_Desc.ElementCount;
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;

	pGraphics->GetDevice()->CreateShaderResourceView(m_pResource, &srvDesc, m_Srv);
}

StructuredBuffer::StructuredBuffer(Graphics* pGraphics)
{
	m_Uav = pGraphics->AllocateCpuDescriptors(1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_Srv = pGraphics->AllocateCpuDescriptors(1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void StructuredBuffer::Create(Graphics* pGraphics, uint32_t elementStride, uint32_t elementCount, bool cpuVisible)
{
	BufferFlag flags = BufferFlag::ShaderResource | BufferFlag::UnorderedAccess;
	if (cpuVisible)
	{
		flags |= BufferFlag::Upload;
	}
	Buffer::Create(pGraphics, BufferDesc::CreateStructured(elementCount, elementStride, flags));
	CreateViews(pGraphics);
}

void StructuredBuffer::CreateViews(Graphics* pGraphics)
{
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Buffer.CounterOffsetInBytes = 0;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	uavDesc.Buffer.NumElements = (uint32_t)m_Desc.ElementCount;
	uavDesc.Buffer.StructureByteStride = m_Desc.ElementSize;
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;

	// structured buffer with counters
	if (m_pCounter == nullptr)
	{
		m_pCounter = std::make_unique<ByteAddressBuffer>(pGraphics);
		m_pCounter->Create(pGraphics, 4, 1, false);
	}

	pGraphics->GetDevice()->CreateUnorderedAccessView(m_pResource, m_pCounter->GetResource(), &uavDesc, m_Uav);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = (uint32_t)m_Desc.ElementCount;
	srvDesc.Buffer.StructureByteStride = m_Desc.ElementSize;
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	pGraphics->GetDevice()->CreateShaderResourceView(m_pResource, &srvDesc, m_Srv);
}
