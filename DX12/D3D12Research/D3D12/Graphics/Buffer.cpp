#include "stdafx.h"
#include "Buffer.h"
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
		m_CurrentState = D3D12_RESOURCE_STATE_GENERIC_READ;
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

void BufferSRV::Create(Graphics* pGraphics, Buffer* pBuffer, const BufferSRVDesc& desc)
{
	assert(pBuffer);
	m_pParent = pBuffer;
	const BufferDesc& bufferDesc = pBuffer->GetDesc();

	if (m_Descriptor.ptr == 0)
	{
		m_Descriptor = pGraphics->AllocateCpuDescriptors(1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}	

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = desc.Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = desc.FirstElement;
	srvDesc.Buffer.NumElements = bufferDesc.ElementCount;
	srvDesc.Buffer.StructureByteStride = 0;

	if (Any(bufferDesc.Usage, BufferFlag::ByteAddress))
	{
		srvDesc.Buffer.Flags |= D3D12_BUFFER_SRV_FLAG_RAW;
	}
	else if (Any(bufferDesc.Usage, BufferFlag::Structured))
	{
		srvDesc.Buffer.StructureByteStride = bufferDesc.ElementSize;
	}

	pGraphics->GetDevice()->CreateShaderResourceView(pBuffer->GetResource(), &srvDesc, m_Descriptor);
}

void BufferUAV::Create(Graphics* pGraphics, Buffer* pBuffer, const BufferUAVDesc& desc)
{
	assert(pBuffer);
	m_pParent = pBuffer;
	const BufferDesc& bufferDesc = pBuffer->GetDesc();

	if (m_Descriptor.ptr == 0)
	{
		m_Descriptor = pGraphics->AllocateCpuDescriptors(1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	uavDesc.Buffer.NumElements = bufferDesc.ElementCount;
	uavDesc.Buffer.StructureByteStride = 0;
	uavDesc.Buffer.FirstElement = desc.FirstElement;
	uavDesc.Format = desc.Format;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;

	if (Any(bufferDesc.Usage, BufferFlag::ByteAddress))
	{
		uavDesc.Buffer.Flags |= D3D12_BUFFER_UAV_FLAG_RAW;
	}
	else if (Any(bufferDesc.Usage, BufferFlag::Structured))
	{
		uavDesc.Buffer.StructureByteStride = bufferDesc.ElementSize;
	}

	pGraphics->GetDevice()->CreateUnorderedAccessView(pBuffer->GetResource(), desc.pCounter ? desc.pCounter->GetResource() : nullptr, &uavDesc, m_Descriptor);
}
