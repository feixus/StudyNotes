#include "stdafx.h"
#include "GraphicsTexture.h"
#include "Content/image.h"
#include "CommandContext.h"
#include "Graphics.h"

GraphicsTexture::GraphicsTexture(ID3D12Device* pDevice)
			: m_Width(0), m_Height(0), m_DepthOrArraySize(0), m_Format(DXGI_FORMAT_UNKNOWN), m_MipLevels(1)
{
	m_SrvUavDescriptorSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_RtvDescriptorSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
}

D3D12_CPU_DESCRIPTOR_HANDLE GraphicsTexture::GetRTV(int subResource) const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_Rtv, subResource, m_SrvUavDescriptorSize);
}

D3D12_CPU_DESCRIPTOR_HANDLE GraphicsTexture::GetUAV(int subResource) const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_Uav, subResource, m_SrvUavDescriptorSize);
}

D3D12_CPU_DESCRIPTOR_HANDLE GraphicsTexture::GetSRV(int subResource) const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_Srv, subResource, m_SrvUavDescriptorSize);
}

D3D12_CPU_DESCRIPTOR_HANDLE GraphicsTexture::GetDSV(int subResource) const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_Rtv, subResource, m_SrvUavDescriptorSize);
}

void GraphicsTexture::Create_Internal(Graphics* pGraphics, TextureDimension dimension, int width, int height, int depthOrArraySize, DXGI_FORMAT format, TextureUsage usage, int sampleCount)
{
	TextureUsage depthAndRt = TextureUsage::RenderTarget | TextureUsage::DepthStencil;
	assert((usage & depthAndRt) != depthAndRt);

	Release();

	m_Width = width;
	m_Height = height;
	m_DepthOrArraySize = depthOrArraySize;
	m_IsArray = dimension == TextureDimension::Texture1DArray || dimension == TextureDimension::Texture2DArray || dimension == TextureDimension::TextureCubeArray;
	m_Format = format;
	m_SampleCount = sampleCount;
	m_CurrentState = D3D12_RESOURCE_STATE_COMMON;

	D3D12_CLEAR_VALUE* pClearValue{};
	D3D12_CLEAR_VALUE clearValue{};
	clearValue.Format = format;

	D3D12_RESOURCE_DESC desc = {};
	desc.Alignment = 0;
	desc.DepthOrArraySize = 1;
	switch (dimension)
	{
	case TextureDimension::Texture1D:
	case TextureDimension::Texture1DArray:
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
		break;
	case TextureDimension::Texture2D:
	case TextureDimension::Texture2DArray:
	case TextureDimension::TextureCube:
	case TextureDimension::TextureCubeArray:
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		break;
	case TextureDimension::Texture3D:
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
		break;
	default:
		assert(false);
		break;
	}

	if ((usage & TextureUsage::UnorderedAccess) == TextureUsage::UnorderedAccess)
	{
		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	}
	if ((usage & TextureUsage::RenderTarget) == TextureUsage::RenderTarget)
	{
		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		Color clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
		memcpy(clearValue.Color, &clearColor, sizeof(Color));
		pClearValue = &clearValue;
	}
	if ((usage & TextureUsage::DepthStencil) == TextureUsage::DepthStencil)
	{
		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		clearValue.DepthStencil.Depth = 0;
		clearValue.DepthStencil.Stencil = 0;
		pClearValue = &clearValue;
	}

	desc.Width = width;
	desc.Height = height;
	desc.Format = format;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.MipLevels = (uint16_t)m_MipLevels;
	desc.SampleDesc.Count = m_SampleCount;
	desc.SampleDesc.Quality = pGraphics->GetMultiSampleQualityLevel(m_SampleCount);
	desc.DepthOrArraySize = m_DepthOrArraySize;

	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	HR(pGraphics->GetDevice()->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		m_CurrentState,
		pClearValue,
		IID_PPV_ARGS(&m_pResource)));

	if ((usage & TextureUsage::ShaderResource) == TextureUsage::ShaderResource)
	{
		if (m_Srv.ptr == 0)
		{
			m_Srv = pGraphics->AllocateCpuDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = (usage & TextureUsage::DepthStencil) == TextureUsage::DepthStencil ? GetSrvFormatFromDepth(format) : format;
		
		switch (dimension)
		{
		case TextureDimension::Texture1D:
			srvDesc.Texture1D.MipLevels = m_MipLevels;
			srvDesc.Texture1D.MostDetailedMip = 0;
			srvDesc.Texture1D.ResourceMinLODClamp = 0;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
			break;
		case TextureDimension::Texture1DArray:
			srvDesc.Texture1DArray.ArraySize = depthOrArraySize;
			srvDesc.Texture1DArray.FirstArraySlice = 0;
			srvDesc.Texture1DArray.MipLevels = m_MipLevels;
			srvDesc.Texture1DArray.MostDetailedMip = 0;
			srvDesc.Texture1DArray.ResourceMinLODClamp = 0;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
			break;
		case TextureDimension::Texture2D:
			if (m_SampleCount > 1)
			{
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
			}
			else
			{
				srvDesc.Texture2D.MipLevels = m_MipLevels;
				srvDesc.Texture2D.MostDetailedMip = 0;
				srvDesc.Texture2D.PlaneSlice = 0;
				srvDesc.Texture2D.ResourceMinLODClamp = 0;
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			}
			break;
		case TextureDimension::Texture2DArray:
			if (sampleCount > 1)
			{
				srvDesc.Texture2DMSArray.ArraySize = depthOrArraySize;
				srvDesc.Texture2DMSArray.FirstArraySlice = 0;
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
			}
			else
			{
				srvDesc.Texture2DArray.MipLevels = m_MipLevels;
				srvDesc.Texture2DArray.MostDetailedMip = 0;
				srvDesc.Texture2DArray.PlaneSlice = 0;
				srvDesc.Texture2DArray.ResourceMinLODClamp = 0;
				srvDesc.Texture2DArray.ArraySize = depthOrArraySize;
				srvDesc.Texture2DArray.FirstArraySlice = 0;
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
			}
			break;
		case TextureDimension::Texture3D:
			srvDesc.Texture3D.MipLevels = m_MipLevels;
			srvDesc.Texture3D.MostDetailedMip = 0;
			srvDesc.Texture3D.ResourceMinLODClamp = 0;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
			break;
		case TextureDimension::TextureCube:
			srvDesc.TextureCube.MipLevels = m_MipLevels;
			srvDesc.TextureCube.MostDetailedMip = 0;
			srvDesc.TextureCube.ResourceMinLODClamp = 0;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			break;
		case TextureDimension::TextureCubeArray:
			srvDesc.TextureCubeArray.MipLevels = m_MipLevels;
			srvDesc.TextureCubeArray.MostDetailedMip = 0;
			srvDesc.TextureCubeArray.ResourceMinLODClamp = 0;
			srvDesc.TextureCubeArray.First2DArrayFace = 0;
			srvDesc.TextureCubeArray.NumCubes = depthOrArraySize;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
			break;
		default:
			break;
		}
		pGraphics->GetDevice()->CreateShaderResourceView(m_pResource.Get(), &srvDesc, m_Srv);
	}
}

void GraphicsTexture2D::Create(Graphics* pGraphics, int width, int height, DXGI_FORMAT format, TextureUsage usage, int sampleCount)
{
	if (m_pResource)
	{
		m_pResource->Release();
	}

	m_Format = format;
	m_SampleCount = sampleCount;

	TextureUsage depthAndRt = TextureUsage::RenderTarget | TextureUsage::DepthStencil;
	assert((usage & depthAndRt) != depthAndRt);

	m_Width = width;
	m_Height = height;

	D3D12_CLEAR_VALUE* pClearValue{};
	D3D12_CLEAR_VALUE clearValue{};
	clearValue.Format = format;

	D3D12_RESOURCE_STATES initState = D3D12_RESOURCE_STATE_COMMON;

	D3D12_RESOURCE_DESC desc = {};
	desc.Alignment = 0;
	desc.DepthOrArraySize = m_DepthOrArraySize;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = width;
	desc.Height = height;
	desc.Format = format;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.MipLevels = (uint16_t)m_MipLevels;
	desc.SampleDesc.Count = m_SampleCount;
	desc.SampleDesc.Quality = pGraphics->GetMultiSampleQualityLevel(m_SampleCount);

	if ((usage & TextureUsage::UnorderedAccess) == TextureUsage::UnorderedAccess)
	{
		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	}
	if ((usage & TextureUsage::RenderTarget) == TextureUsage::RenderTarget)
	{
		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		Color clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
		memcpy(clearValue.Color, &clearColor, sizeof(Color));
		pClearValue = &clearValue;
	}
	if ((usage & TextureUsage::DepthStencil) == TextureUsage::DepthStencil)
	{
		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		clearValue.DepthStencil.Depth = 0;
		clearValue.DepthStencil.Stencil = 0;
		pClearValue = &clearValue;
	}

	desc.Format = format;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.MipLevels = (uint16_t)m_MipLevels;
	desc.SampleDesc.Count = m_SampleCount;
	desc.SampleDesc.Quality = pGraphics->GetMultiSampleQualityLevel(m_SampleCount);

	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	HR(pGraphics->GetDevice()->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		initState,
		pClearValue,
		IID_PPV_ARGS(&m_pResource)));
	m_CurrentState = initState;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	if ((usage & TextureUsage::DepthStencil) == TextureUsage::DepthStencil)
	{
		srvDesc.Format = GetDepthFormat(format);
	}
	else
	{
		srvDesc.Format = format;
	}

	srvDesc.Texture2D.MipLevels = m_MipLevels;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.PlaneSlice = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0;
	if (m_SampleCount > 1)
	{
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
	}
	else
	{
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	}

	if ((usage & TextureUsage::ShaderResource) == TextureUsage::ShaderResource)
	{
		if (m_Srv.ptr == 0)
		{
			m_Srv = pGraphics->AllocateCpuDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}
		if (m_SampleCount > 1)
		{
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
		}
		pGraphics->GetDevice()->CreateShaderResourceView(m_pResource, &srvDesc, m_Srv);
	}
	if ((usage & TextureUsage::UnorderedAccess) == TextureUsage::UnorderedAccess)
	{
		if (m_Uav.ptr == 0)
		{
			m_Uav = pGraphics->AllocateCpuDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}
		pGraphics->GetDevice()->CreateUnorderedAccessView(m_pResource, nullptr, nullptr, m_Uav);
	}
	if ((usage & TextureUsage::RenderTarget) == TextureUsage::RenderTarget)
	{
		if (m_Rtv.ptr == 0)
		{
			m_Rtv = pGraphics->AllocateCpuDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		}
		pGraphics->GetDevice()->CreateRenderTargetView(m_pResource, nullptr, m_Rtv);
	}
	else if ((usage & TextureUsage::DepthStencil) == TextureUsage::DepthStencil)
	{
		if (m_Rtv.ptr == 0)
		{
			m_Rtv = pGraphics->AllocateCpuDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
		}
		pGraphics->GetDevice()->CreateDepthStencilView(m_pResource, nullptr, m_Rtv);
	}
}

void GraphicsTexture2D::Create(Graphics* pGraphics, CommandContext* pContext, const char* filePath, TextureUsage usage)
{
	Image img;
	if (img.Load(filePath))
	{
		m_Width = img.GetWidth();
		m_Height = img.GetHeight();
		m_Format = (DXGI_FORMAT)Image::TextureFormatFromCompressionFormat(img.GetFormat(), false);
		m_MipLevels = img.GetMipLevels();

		std::vector<D3D12_SUBRESOURCE_DATA> subresources(m_MipLevels);
		for (int i = 0; i < m_MipLevels; i++)
		{
			D3D12_SUBRESOURCE_DATA& data = subresources[i];
			MipLevelInfo mipLevelInfo = img.GetMipLevelInfo(i);
			data.pData = img.GetData(i);
			data.RowPitch = mipLevelInfo.RowSize;
			data.SlicePitch = mipLevelInfo.RowSize * mipLevelInfo.Height;
		}

		Create(pGraphics, m_Width, m_Height, m_Format, usage, 1);
		pContext->InitializeTexture(this, subresources.data(), m_MipLevels);
		pContext->ExecuteAndReset(true);
	}
}

void GraphicsTexture2D::SetData(CommandContext* pContext, const void* pData)
{
	D3D12_SUBRESOURCE_DATA data;
	data.pData = pData;
	data.RowPitch = GetRowDataSize(m_Width);
	data.SlicePitch = data.RowPitch * m_Height;
	pContext->InitializeTexture(this, &data, 1);
}

void GraphicsTexture2D::CreateForSwapChain(Graphics* pGraphics, ID3D12Resource* pTexture)
{
	m_pResource = pTexture;
	m_CurrentState = D3D12_RESOURCE_STATE_PRESENT;

	D3D12_RESOURCE_DESC desc = pTexture->GetDesc();
	m_Width = (uint32_t)desc.Width;
	m_Height = (uint32_t)desc.Height;
	m_Format = desc.Format;
	m_Rtv = pGraphics->AllocateCpuDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	pGraphics->GetDevice()->CreateRenderTargetView(m_pResource, nullptr, m_Rtv);
}

DXGI_FORMAT GraphicsTexture::GetDepthFormat(DXGI_FORMAT format)
{
	switch (format)
	{
		// 32-bit Z w/ Stencil
	case DXGI_FORMAT_R32G8X24_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
	case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
	case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
		return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;

		// No Stencil
	case DXGI_FORMAT_R32_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT:
	case DXGI_FORMAT_R32_FLOAT:
		return DXGI_FORMAT_R32_FLOAT;

		// 24-bit Z
	case DXGI_FORMAT_R24G8_TYPELESS:
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
	case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
	case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
		return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;

		// 16-bit Z w/o Stencil
	case DXGI_FORMAT_R16_TYPELESS:
	case DXGI_FORMAT_D16_UNORM:
	case DXGI_FORMAT_R16_UNORM:
		return DXGI_FORMAT_R16_UNORM;

	default:
		return DXGI_FORMAT_UNKNOWN;
	}
}

int GraphicsTexture::GetRowDataSize(unsigned int width) const
{
	switch (m_Format)
	{
	case DXGI_FORMAT_R8_UNORM:
	case DXGI_FORMAT_A8_UNORM:
		return (unsigned)width;

	case DXGI_FORMAT_R8G8_UNORM:
	case DXGI_FORMAT_R16_UNORM:
	case DXGI_FORMAT_R16_FLOAT:
	case DXGI_FORMAT_R16_TYPELESS:
		return (unsigned)(width * 2);

	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R16G16_UNORM:
	case DXGI_FORMAT_R16G16_FLOAT:
	case DXGI_FORMAT_R32_FLOAT:
	case DXGI_FORMAT_R24G8_TYPELESS:
	case DXGI_FORMAT_R32_TYPELESS:
		return (unsigned)(width * 4);

	case DXGI_FORMAT_R16G16B16A16_UNORM:
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
		return (unsigned)(width * 8);

	case DXGI_FORMAT_R32G32B32A32_FLOAT:
		return (unsigned)(width * 16);

	case DXGI_FORMAT_BC1_TYPELESS:
	case DXGI_FORMAT_BC1_UNORM:
	case DXGI_FORMAT_BC1_UNORM_SRGB:
	case DXGI_FORMAT_BC4_TYPELESS:
	case DXGI_FORMAT_BC4_UNORM:
	case DXGI_FORMAT_BC4_SNORM:
		return (unsigned)(((width + 3) >> 2) * 8);

	case DXGI_FORMAT_BC2_TYPELESS:
	case DXGI_FORMAT_BC2_UNORM:
	case DXGI_FORMAT_BC2_UNORM_SRGB:
	case DXGI_FORMAT_BC3_TYPELESS:
	case DXGI_FORMAT_BC3_UNORM:
	case DXGI_FORMAT_BC3_UNORM_SRGB:
	case DXGI_FORMAT_BC5_TYPELESS:
	case DXGI_FORMAT_BC5_UNORM:
	case DXGI_FORMAT_BC5_SNORM:
	case DXGI_FORMAT_BC6H_TYPELESS:
	case DXGI_FORMAT_BC6H_UF16:
	case DXGI_FORMAT_BC6H_SF16:
	case DXGI_FORMAT_BC7_TYPELESS:
	case DXGI_FORMAT_BC7_UNORM:
	case DXGI_FORMAT_BC7_UNORM_SRGB:
		return (unsigned)(((width + 3) >> 2) * 16);
	case DXGI_FORMAT_R32G32B32_FLOAT:
		return width * 3 * sizeof(float);
	default:
		return 0;

	}
}

void GraphicsTextureCube::Create(Graphics* pGraphics, int width, int height, DXGI_FORMAT format, TextureUsage usage, int sampleCount)
{
	if (m_pResource)
	{
		m_pResource->Release();
	}

	TextureUsage depthAndRt = TextureUsage::RenderTarget | TextureUsage::DepthStencil;
	assert((usage & depthAndRt) != depthAndRt);
	assert((usage & TextureUsage::UnorderedAccess) != TextureUsage::UnorderedAccess);

	m_Width = width;
	m_Height = height;
	m_Format = format;
	m_SampleCount = sampleCount;

	D3D12_CLEAR_VALUE* pClearValue{};
	D3D12_CLEAR_VALUE clearValue{};
	clearValue.Format = format;

	D3D12_RESOURCE_STATES initState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

	D3D12_RESOURCE_DESC desc{};
	desc.Alignment = 0;
	desc.DepthOrArraySize = 6;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	if ((usage & TextureUsage::RenderTarget) == TextureUsage::RenderTarget)
	{
		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		Color clearColor = Color(0, 0, 0, 1);
		memcpy(&clearValue.Color, &clearColor, sizeof(Color));
		pClearValue = &clearValue;
		initState = D3D12_RESOURCE_STATE_RENDER_TARGET;
	}
	if ((usage & TextureUsage::DepthStencil) == TextureUsage::DepthStencil)
	{
		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		clearValue.DepthStencil.Depth = 1.0f;
		clearValue.DepthStencil.Stencil = 0;
		pClearValue = &clearValue;
		initState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	}

	desc.Format = format;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.MipLevels = (uint16_t)m_MipLevels;
	desc.Width = (uint32_t)width;
	desc.Height = (uint32_t)height;
	desc.SampleDesc.Count = sampleCount;
	desc.SampleDesc.Quality = pGraphics->GetMultiSampleQualityLevel(sampleCount);

	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	HR(pGraphics->GetDevice()->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		initState,
		pClearValue,
		IID_PPV_ARGS(&m_pResource)));
	m_CurrentState = initState;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	if ((usage & TextureUsage::DepthStencil) == TextureUsage::DepthStencil)
	{
		srvDesc.Format = GetDepthFormat(format);
	}
	else
	{
		srvDesc.Format = format;
	}

	srvDesc.TextureCube.MipLevels = m_MipLevels;
	srvDesc.TextureCube.MostDetailedMip = 0;
	srvDesc.TextureCube.ResourceMinLODClamp = 0;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;

	if ((usage & TextureUsage::ShaderResource) == TextureUsage::ShaderResource)
	{
		if (m_Srv.ptr == 0)
		{
			m_Srv = pGraphics->AllocateCpuDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}
		pGraphics->GetDevice()->CreateShaderResourceView(m_pResource, &srvDesc, m_Srv);
	}

	if ((usage & TextureUsage::RenderTarget) == TextureUsage::RenderTarget)
	{
		for (int i = 0; i < (int)CubeMapFace::MAX; i++)
		{
			if (m_Rtv[i].ptr == 0)
			{
				m_Rtv[i] = pGraphics->AllocateCpuDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			}

			D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
			rtvDesc.Texture2D.PlaneSlice = i;
			rtvDesc.Texture2D.MipSlice = 0;
			rtvDesc.Format = format;
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
			pGraphics->GetDevice()->CreateRenderTargetView(m_pResource, &rtvDesc, m_Rtv[i]);
		}
	}
	else if ((usage & TextureUsage::DepthStencil) == TextureUsage::DepthStencil)
	{
		for (int i = 0; i < (int)CubeMapFace::MAX; i++)
		{
			if (m_Rtv[i].ptr == 0)
			{
				m_Rtv[i] = pGraphics->AllocateCpuDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
			}

			D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
			dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
			dsvDesc.Format = GetDepthFormat(format);
			dsvDesc.Texture2D.MipSlice = 0;
			dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			pGraphics->GetDevice()->CreateDepthStencilView(m_pResource, &dsvDesc, m_Rtv[i]);
		}
	}
}


