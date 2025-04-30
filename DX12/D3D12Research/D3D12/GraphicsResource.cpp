#include "stdafx.h"
#include "GraphicsResource.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "External/stb/stb_image.h"

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

void GraphicsTexture::Create(Graphics* pGraphics, int width, int height)
{
	m_Width = width;
	m_Height = height;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Alignment = 0;
	desc.DepthOrArraySize = 1;
	desc.Width = width;
	desc.Height = height;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;

	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	HR(pGraphics->GetDevice()->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_pResource)));
	m_CurrentState = D3D12_RESOURCE_STATE_GENERIC_READ;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.PlaneSlice = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;

	m_CpuDescriptorHandle = pGraphics->AllocateCpuDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	pGraphics->GetDevice()->CreateShaderResourceView(m_pResource, &srvDesc, m_CpuDescriptorHandle);
}

void GraphicsTexture::Create(Graphics* pGraphics, CommandContext* pContext, const char* filePath)
{
	int components = 0;
	void* pPixels = stbi_load(filePath, &m_Width, &m_Height, &components, STBI_rgb_alpha);
	Create(pGraphics, m_Width, m_Height);
	SetData(pContext, pPixels, m_Width * m_Height * 4);
	pContext->ExecuteAndReset(true);
	stbi_image_free(pPixels);
}

void GraphicsTexture::SetData(CommandContext* pContext, void* pData, uint32_t dataSize)
{
	assert((uint32_t)(m_Width * m_Height * 4) == dataSize);
	pContext->InitializeTexture(this, pData, dataSize);
}
