#include "stdafx.h"
#include "GraphicsTexture.h"
#include "Content/image.h"
#include "CommandContext.h"
#include "Graphics.h"

GraphicsTexture::GraphicsTexture()
			: m_Width(0), m_Height(0), m_DepthOrArraySize(0), m_Format(DXGI_FORMAT_UNKNOWN), m_MipLevels(1), m_Dimension(TextureDimension::Texture2D), m_IsArray(false)
{
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

	m_SrvUavDescriptorSize = pGraphics->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_RtvDescriptorSize = pGraphics->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

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
	desc.Width = width;
	desc.Height = height;
	desc.Format = format;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.MipLevels = (uint16_t)m_MipLevels;
	desc.SampleDesc.Count = m_SampleCount;
	desc.SampleDesc.Quality = pGraphics->GetMultiSampleQualityLevel(m_SampleCount);

	switch (dimension)
	{
	case TextureDimension::Texture1D:
	case TextureDimension::Texture1DArray:
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
		desc.DepthOrArraySize = depthOrArraySize;
		break;
	case TextureDimension::Texture2D:
	case TextureDimension::Texture2DArray:
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.DepthOrArraySize = depthOrArraySize;
		break;
	case TextureDimension::TextureCube:
	case TextureDimension::TextureCubeArray:
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.DepthOrArraySize = 6 * depthOrArraySize;
		break;
	case TextureDimension::Texture3D:
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
		desc.DepthOrArraySize = depthOrArraySize;
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
		clearValue.DepthStencil.Depth = 1;
		clearValue.DepthStencil.Stencil = 0;
		pClearValue = &clearValue;
	}

	m_pResource = pGraphics->CreateResource(desc, m_CurrentState, D3D12_HEAP_TYPE_DEFAULT, pClearValue);

	if ((usage & TextureUsage::ShaderResource) == TextureUsage::ShaderResource)
	{
		if (m_Srv.ptr == 0)
		{
			m_Srv = pGraphics->AllocateCpuDescriptors(1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
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
		pGraphics->GetDevice()->CreateShaderResourceView(m_pResource, &srvDesc, m_Srv);
	}

	if ((usage & TextureUsage::UnorderedAccess) == TextureUsage::UnorderedAccess)
	{
		if (m_Uav.ptr == 0)
		{
			m_Uav = pGraphics->AllocateCpuDescriptors(m_MipLevels, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}

		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		switch (dimension)
		{
		case TextureDimension::Texture1D:
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
			break;
		case TextureDimension::Texture1DArray:
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
			break;
		case TextureDimension::Texture2D:
			uavDesc.Texture2D.PlaneSlice = 0;
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			break;
		case TextureDimension::Texture2DArray:
			uavDesc.Texture2DArray.ArraySize = depthOrArraySize;
			uavDesc.Texture2DArray.FirstArraySlice = 0;
			uavDesc.Texture2DArray.PlaneSlice = 0;
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
			break;
		case TextureDimension::Texture3D:
			uavDesc.Texture3D.FirstWSlice = 0;
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
			break;
		case TextureDimension::TextureCube:
		case TextureDimension::TextureCubeArray:
			uavDesc.Texture2DArray.ArraySize = depthOrArraySize * 6;
			uavDesc.Texture2DArray.FirstArraySlice = 0;
			uavDesc.Texture2DArray.PlaneSlice = 0;
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
			break;
		default:
			break;
		}

		// one view to one mip level and one array slice
		for (int i = 0; i < m_MipLevels; ++i)
		{
			uavDesc.Texture1D.MipSlice = i;
			uavDesc.Texture1DArray.MipSlice = i;
			uavDesc.Texture2D.MipSlice = i;
			uavDesc.Texture2DArray.MipSlice = i;
			uavDesc.Texture3D.MipSlice = i;
			pGraphics->GetDevice()->CreateUnorderedAccessView(m_pResource, nullptr, &uavDesc, CD3DX12_CPU_DESCRIPTOR_HANDLE(m_Uav, i, m_SrvUavDescriptorSize));
		}
	}

	if ((usage & TextureUsage::RenderTarget) == TextureUsage::RenderTarget)
	{
		if (m_Rtv.ptr == 0)
		{
			m_Rtv = pGraphics->AllocateCpuDescriptors(1, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		}

		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
		rtvDesc.Format = format;
		switch (dimension)
		{
		case TextureDimension::Texture1D:
			rtvDesc.Texture2D.MipSlice = 0;
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
			break;
		case TextureDimension::Texture1DArray:
			rtvDesc.Texture1DArray.ArraySize = depthOrArraySize;
			rtvDesc.Texture1DArray.FirstArraySlice = 0;
			rtvDesc.Texture1DArray.MipSlice = 0;
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
			break;
		case TextureDimension::Texture2D:
			if (sampleCount > 1)
			{
				rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
			}
			else
			{
				rtvDesc.Texture2D.MipSlice = 0;
				rtvDesc.Texture2D.PlaneSlice = 0;
				rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
			}
			break;
		case TextureDimension::TextureCube:
		case TextureDimension::TextureCubeArray:
		case TextureDimension::Texture2DArray:
			if (sampleCount > 1)
			{
				rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
			}
			else
			{
				rtvDesc.Texture2DArray.ArraySize = depthOrArraySize;
				rtvDesc.Texture2DArray.FirstArraySlice = 0;
				rtvDesc.Texture2DArray.PlaneSlice = 0;
				rtvDesc.Texture2DArray.MipSlice = 0;
				rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
			}
			break;
		case TextureDimension::Texture3D:
			rtvDesc.Texture3D.FirstWSlice = 0;
			rtvDesc.Texture3D.MipSlice = 0;
			rtvDesc.Texture3D.WSize = depthOrArraySize;
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
			break;
		default:
			break;
		}
		
		pGraphics->GetDevice()->CreateRenderTargetView(m_pResource, &rtvDesc, m_Rtv);
	}
	else if ((usage & TextureUsage::DepthStencil) == TextureUsage::DepthStencil)
	{
		if (m_Rtv.ptr == 0)
		{
			m_Rtv = pGraphics->AllocateCpuDescriptors(1, D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
		}

		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
		dsvDesc.Format = format;
		switch (dimension)
		{
		case TextureDimension::Texture1D:
			dsvDesc.Texture2D.MipSlice = 0;
			dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
			break;
		case TextureDimension::Texture1DArray:
			dsvDesc.Texture1DArray.ArraySize = depthOrArraySize;
			dsvDesc.Texture1DArray.FirstArraySlice = 0;
			dsvDesc.Texture1DArray.MipSlice = 0;
			dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
			break;
		case TextureDimension::Texture2D:
			if (sampleCount > 1)
			{
				dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
			}
			else
			{
				dsvDesc.Texture2D.MipSlice = 0;
				dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			}
			break;
		case TextureDimension::Texture3D:
		case TextureDimension::TextureCube:
		case TextureDimension::TextureCubeArray:
		case TextureDimension::Texture2DArray:
			if (sampleCount > 1)
			{
				dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
			}
			else
			{
				dsvDesc.Texture2DArray.ArraySize = depthOrArraySize;
				dsvDesc.Texture2DArray.FirstArraySlice = 0;
				dsvDesc.Texture2DArray.MipSlice = 0;
				dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
			}
			break;
		default:
			break;
		}

		pGraphics->GetDevice()->CreateDepthStencilView(m_pResource, &dsvDesc, m_Rtv);
	}
}

DXGI_FORMAT GraphicsTexture::GetSrvFormatFromDepth(DXGI_FORMAT format)
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

int GraphicsTexture::GetRowDataSize(DXGI_FORMAT format, unsigned int width)
{
	switch (format)
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
		assert(false);
		return 0;

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
		pContext->InitializeTexture(this, subresources.data(), 0, m_MipLevels);
		pContext->ExecuteAndReset(true);
	}
}

void GraphicsTexture2D::Create(Graphics* pGraphics, int width, int height, DXGI_FORMAT format, TextureUsage usage, int sampleCount, int arraySize)
{
	if (arraySize != -1)
	{
		Create_Internal(pGraphics, TextureDimension::Texture2DArray, width, height, arraySize, format, usage, sampleCount);
	}
	else
	{
		Create_Internal(pGraphics, TextureDimension::Texture2D, width, height, 1, format, usage, sampleCount);
	}
}

void GraphicsTexture2D::SetData(CommandContext* pContext, const void* pData)
{
	D3D12_SUBRESOURCE_DATA data;
	data.pData = pData;
	data.RowPitch = GetRowDataSize(m_Format, m_Width);
	data.SlicePitch = data.RowPitch * m_Height;
	pContext->InitializeTexture(this, &data, 0, 1);
}

void GraphicsTexture2D::CreateForSwapChain(Graphics* pGraphics, ID3D12Resource* pTexture)
{
	Release();

	m_pResource = pTexture;
	m_CurrentState = D3D12_RESOURCE_STATE_PRESENT;

	D3D12_RESOURCE_DESC desc = pTexture->GetDesc();
	m_Width = (uint32_t)desc.Width;
	m_Height = (uint32_t)desc.Height;
	m_Format = desc.Format;
	m_Rtv = pGraphics->AllocateCpuDescriptors(1, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	pGraphics->GetDevice()->CreateRenderTargetView(m_pResource, nullptr, m_Rtv);
}

void GraphicsTextureCube::Create(Graphics* pGraphics, CommandContext* pContext, const char* pFilePath, TextureUsage usage)
{
	Image img;
	if (img.Load(pFilePath))
	{
		m_MipLevels = img.GetMipLevels();
		std::vector<D3D12_SUBRESOURCE_DATA> subResourceData(m_MipLevels * 6);
		const Image* pCurrentImage = &img;
		for (int i = 0; i < 6; i++)
		{
			assert(pCurrentImage);
			for (int j = 0; j < m_MipLevels; j++)
			{
				D3D12_SUBRESOURCE_DATA data = subResourceData[i * m_MipLevels + j];
				MipLevelInfo info = pCurrentImage->GetMipLevelInfo(j);
				data.pData = pCurrentImage->GetData(j);
				data.RowPitch = info.RowSize;
				data.SlicePitch = (uint64_t)info.RowSize * info.Width;
			}
			pCurrentImage = pCurrentImage->GetNextImage();
		}

		DXGI_FORMAT format = (DXGI_FORMAT)Image::TextureFormatFromCompressionFormat(img.GetFormat(), false);
		Create_Internal(pGraphics, TextureDimension::TextureCube, img.GetWidth(), img.GetHeight(), 1, format, usage, 1);
		pContext->InitializeTexture(this, subResourceData.data(), 0, m_MipLevels * 6);
		pContext->ExecuteAndReset(true);
	}
}

void GraphicsTextureCube::Create(Graphics* pGraphics, int width, int height, DXGI_FORMAT format, TextureUsage usage, int sampleCount, int arraySize)
{
	if (arraySize != -1)
	{
		Create_Internal(pGraphics, TextureDimension::TextureCubeArray, width, height, arraySize, format, usage, sampleCount);
	}
	else
	{
		Create_Internal(pGraphics, TextureDimension::TextureCube, width, height, 1, format, usage, sampleCount);
	}
}

void GraphicsTexture3D::Create(Graphics* pGraphics, int width, int height, int depth, DXGI_FORMAT format, TextureUsage usage)
{
	Create_Internal(pGraphics, TextureDimension::Texture3D, width, height, depth, format, usage, 1);
}
