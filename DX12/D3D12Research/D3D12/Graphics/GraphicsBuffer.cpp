#include "stdafx.h"
#include "GraphicsBuffer.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "GraphicsTexture.h"
#include "ResourceViews.h"

Buffer::Buffer(Graphics* pGraphics, ID3D12Resource* pResource, D3D12_RESOURCE_STATES state)
	: GraphicsResource(pGraphics, pResource, state)
{}

Buffer::Buffer(Graphics* pGraphics, const char* pName) 
	: GraphicsResource(pGraphics), m_pName(pName)
{}

Buffer::~Buffer()
{}

void Buffer::Create(const BufferDesc& bufferDesc)
{
	Release();
	m_Desc = bufferDesc;

	int64_t width = Math::AlignUp<int64_t>((int64_t)bufferDesc.ElementSize * bufferDesc.ElementCount, 16);
	D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(width);

	D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT;
	if (Any(bufferDesc.Usage, BufferFlag::ShaderResource | BufferFlag::AccelerationStructure) == false)
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
	if (Any(bufferDesc.Usage, BufferFlag::AccelerationStructure))
	{
		m_CurrentState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
	}

	m_pResource = m_pGraphics->CreateResource(desc, m_CurrentState, heapType);

	if (m_pName)
	{
		SetName(m_pName);
	}

	if (Any(bufferDesc.Usage, BufferFlag::UnorderedAccess))
	{
		if (Any(bufferDesc.Usage, BufferFlag::Structured))
		{
			CreateUAV(&m_pUav, BufferUAVDesc(DXGI_FORMAT_UNKNOWN, false, true));
		}
		else
		{
			CreateUAV(&m_pUav, BufferUAVDesc(DXGI_FORMAT_UNKNOWN, true, false));
		}
	}

	if (Any(bufferDesc.Usage, BufferFlag::ShaderResource))
	{
		CreateSRV(&m_pSrv, BufferSRVDesc(DXGI_FORMAT_UNKNOWN));
	}
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

void Buffer::CreateUAV(UnorderedAccessView** pView, const BufferUAVDesc& desc)
{
	if (*pView == nullptr)
	{
		m_Descriptors.push_back(std::make_unique<UnorderedAccessView>(m_pGraphics));
		*pView = static_cast<UnorderedAccessView*>(m_Descriptors.back().get());
	}

	(*pView)->Create(this, desc);
}

void Buffer::CreateSRV(ShaderResourceView** pView, const BufferSRVDesc& desc)
{
	if (*pView == nullptr)
	{
		m_Descriptors.push_back(std::make_unique<ShaderResourceView>(m_pGraphics));
		*pView = static_cast<ShaderResourceView*>(m_Descriptors.back().get());
	}

	(*pView)->Create(this, desc);
}
