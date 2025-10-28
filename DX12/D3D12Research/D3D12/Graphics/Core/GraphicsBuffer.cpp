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
		Math::AlignUp<int64_t>((int64_t)bufferDesc.Size, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT),
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
	D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_UNKNOWN;

	if (Any(bufferDesc.Usage, BufferFlag::Readback))
	{
		check(initialState == D3D12_RESOURCE_STATE_UNKNOWN);
		initialState = D3D12_RESOURCE_STATE_COPY_DEST;
		heapType = D3D12_HEAP_TYPE_READBACK;
	}
	else if (Any(bufferDesc.Usage, BufferFlag::Upload))
	{
		check(initialState == D3D12_RESOURCE_STATE_UNKNOWN);
		initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
		heapType = D3D12_HEAP_TYPE_UPLOAD;
	}
	if (Any(bufferDesc.Usage, BufferFlag::AccelerationStructure))
	{
		check(initialState == D3D12_RESOURCE_STATE_UNKNOWN);
		initialState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
	}

	if (initialState == D3D12_RESOURCE_STATE_UNKNOWN)
	{
		initialState = D3D12_RESOURCE_STATE_COMMON;
	}
	
	m_pResource = GetGraphics()->CreateResource(desc, initialState, heapType);
	SetResourceState(initialState);

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

void Buffer::SetData(CommandContext* pContext, const void* pData, uint64_t dataSize, uint64_t offset /*= 0*/)
{
	check(dataSize + offset <= GetSize());
	pContext->InitializeBuffer(this, pData, dataSize, offset);
}

void Buffer::CreateUAV(UnorderedAccessView** pView, const BufferUAVDesc& desc)
{
	if (*pView == nullptr)
	{
		m_Descriptors.push_back(std::make_unique<UnorderedAccessView>());
		*pView = static_cast<UnorderedAccessView*>(m_Descriptors.back().get());
	}

	(*pView)->Create(this, desc);
}

void Buffer::CreateSRV(ShaderResourceView** pView, const BufferSRVDesc& desc)
{
	if (*pView == nullptr)
	{
		m_Descriptors.push_back(std::make_unique<ShaderResourceView>());
		*pView = static_cast<ShaderResourceView*>(m_Descriptors.back().get());
	}

	(*pView)->Create(this, desc);
}
