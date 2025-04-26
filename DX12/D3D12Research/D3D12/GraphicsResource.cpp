#include "stdafx.h"
#include "GraphicsResource.h"
#include "CommandContext.h"

void GraphicsBuffer::Create(ID3D12Device* pDevice, uint32_t size, bool cpuVisible)
{
	m_Size = size;

	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Alignment = 0;
	desc.DepthOrArraySize = 1;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE;
	desc.Width = size;
	desc.Height = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;

	D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(cpuVisible ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT);

	HR(pDevice->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_pResource)));

	m_CurrentState = D3D12_RESOURCE_STATE_GENERIC_READ;
}

void GraphicsBuffer::SetData(CommandContext* pContext, void* pData, uint32_t dataSize)
{
	assert(m_Size == dataSize);
	pContext->AllocateUploadMemory(dataSize);
	pContext->InitializeBuffer(this, pData, dataSize);
}
