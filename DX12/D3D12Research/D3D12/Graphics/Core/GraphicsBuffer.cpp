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
	: GraphicsResource(pGraphics), m_Name(pName)
{}

Buffer::~Buffer()
{}

D3D12_RESOURCE_DESC GetResourceDesc(const BufferDesc& bufferDesc)
{
	D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(
		Math::AlignUp<int64_t>((int64_t)bufferDesc.ElementSize * bufferDesc.ElementCount, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT),
		D3D12_RESOURCE_FLAG_NONE
	);

	if (!Any(bufferDesc.Usage, BufferFlag::ShaderResource | BufferFlag::AccelerationStructure))
	{
		desc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
	}
	if (Any(bufferDesc.Usage, BufferFlag::UnorderedAccess))
	{
		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	}
	return desc;
}

void Buffer::Create(const BufferDesc& bufferDesc)
{
	Release();
	m_Desc = bufferDesc;

	D3D12_RESOURCE_DESC desc = GetResourceDesc(bufferDesc);
	D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT;

	if (Any(bufferDesc.Usage, BufferFlag::Readback))
	{
		SetResourceState(D3D12_RESOURCE_STATE_COPY_DEST);
		heapType = D3D12_HEAP_TYPE_READBACK;
	}
	else if (Any(bufferDesc.Usage, BufferFlag::Upload))
	{
		SetResourceState(D3D12_RESOURCE_STATE_GENERIC_READ);
		heapType = D3D12_HEAP_TYPE_UPLOAD;
	}
	if (Any(bufferDesc.Usage, BufferFlag::AccelerationStructure))
	{
		SetResourceState(D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
	}

	m_pResource = m_pGraphics->CreateResource(desc, GetResourceState(), heapType);

	SetName(m_Name.c_str());

	if (Any(bufferDesc.Usage, BufferFlag::UnorderedAccess))
	{
		// structured buffer
		if (Any(bufferDesc.Usage, BufferFlag::Structured))
		{
			CreateUAV(&m_pUav, BufferUAVDesc(DXGI_FORMAT_UNKNOWN, false, true));
		}
		// byteAddress buffer
		else if (Any(bufferDesc.Usage, BufferFlag::ByteAddress))
		{
			CreateUAV(&m_pUav, BufferUAVDesc(DXGI_FORMAT_UNKNOWN, true, false));
		}
		// typed buffer
		else
		{
			CreateUAV(&m_pUav, BufferUAVDesc(DXGI_FORMAT_UNKNOWN, false, false));
		}
	}

	if (Any(bufferDesc.Usage, BufferFlag::ShaderResource | BufferFlag::AccelerationStructure))
	{
		if (Any(bufferDesc.Usage, BufferFlag::Structured))
		{
			CreateSRV(&m_pSrv, BufferSRVDesc(DXGI_FORMAT_UNKNOWN, false));
		}
		else if (Any(bufferDesc.Usage, BufferFlag::ByteAddress))
		{
			CreateSRV(&m_pSrv, BufferSRVDesc(DXGI_FORMAT_UNKNOWN, true));
		}
		else
		{
			CreateSRV(&m_pSrv, BufferSRVDesc(bufferDesc.Format));
		}
	}
}

void Buffer::SetData(CommandContext* pContext, const void* pData, uint64_t dataSize, uint32_t offset /*= 0*/)
{
	check(dataSize + offset <= GetSize());
	pContext->InitializeBuffer(this, pData, dataSize, offset);
}

void* Buffer::Map(uint32_t subResource /*= 0*/, uint64_t readFrom /*= 0*/, uint64_t readTo /*= 0*/)
{
	check(m_pResource);

	CD3DX12_RANGE range(readFrom, readTo);
	void* pMappedData = nullptr;
	m_pResource->Map(subResource, &range, &pMappedData);
	return pMappedData;
}

void Buffer::UnMap(uint32_t subResource /*= 0*/, uint64_t writeFrom /*= 0*/, uint64_t writeTo /*= 0*/)
{
	check(m_pResource);

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
