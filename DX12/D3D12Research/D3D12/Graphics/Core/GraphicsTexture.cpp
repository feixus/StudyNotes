#include "stdafx.h"
#include "GraphicsTexture.h"
#include "Content/image.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "OfflineDescriptorAllocator.h"
#include "ResourceViews.h"

GraphicsTexture::GraphicsTexture(Graphics* pGraphics, const char* pName)
	: GraphicsResource(pGraphics), m_Name(pName)
{
}

GraphicsTexture::~GraphicsTexture()
{
}

D3D12_CPU_DESCRIPTOR_HANDLE GraphicsTexture::GetRTV() const
{
	return m_Rtv;
}

D3D12_CPU_DESCRIPTOR_HANDLE GraphicsTexture::GetUAV() const
{
	return m_pUav->GetDescriptor();
}

D3D12_CPU_DESCRIPTOR_HANDLE GraphicsTexture::GetSRV() const
{
	return m_pSrv->GetDescriptor();
}

D3D12_CPU_DESCRIPTOR_HANDLE GraphicsTexture::GetDSV(bool writeable) const
{
	return writeable ? m_Rtv : m_ReadOnlyDsv;
}

D3D12_RESOURCE_DESC GetResourceDesc(const TextureDesc& textureDesc)
{
	uint32_t width = D3D::IsBlockCompressFormat(textureDesc.Format) ? Math::Clamp(textureDesc.Width, 0, textureDesc.Width) : textureDesc.Width;
	uint32_t height = D3D::IsBlockCompressFormat(textureDesc.Format) ? Math::Clamp(textureDesc.Height, 0, textureDesc.Height) : textureDesc.Height;
	
	D3D12_RESOURCE_DESC desc{};
	switch (textureDesc.Dimension)
	{
	case TextureDimension::Texture1D:
	case TextureDimension::Texture1DArray:
		desc = CD3DX12_RESOURCE_DESC::Tex1D(textureDesc.Format, width, textureDesc.DepthOrArraySize, textureDesc.Mips, D3D12_RESOURCE_FLAG_NONE, D3D12_TEXTURE_LAYOUT_UNKNOWN);
		break;
	case TextureDimension::Texture2D:
	case TextureDimension::Texture2DArray:
		desc = CD3DX12_RESOURCE_DESC::Tex2D(textureDesc.Format, width, height, textureDesc.DepthOrArraySize, textureDesc.Mips, textureDesc.SampleCount, 0, D3D12_RESOURCE_FLAG_NONE, D3D12_TEXTURE_LAYOUT_UNKNOWN);
		break;
	case TextureDimension::TextureCube:
	case TextureDimension::TextureCubeArray:
		desc = CD3DX12_RESOURCE_DESC::Tex2D(textureDesc.Format, width, height, textureDesc.DepthOrArraySize * 6, textureDesc.Mips, D3D12_RESOURCE_FLAG_NONE, D3D12_TEXTURE_LAYOUT_UNKNOWN);
		break;
	case TextureDimension::Texture3D:
		desc = CD3DX12_RESOURCE_DESC::Tex3D(textureDesc.Format, width, height, textureDesc.DepthOrArraySize, textureDesc.Mips, D3D12_RESOURCE_FLAG_NONE, D3D12_TEXTURE_LAYOUT_UNKNOWN);
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
		desc.DepthOrArraySize = textureDesc.DepthOrArraySize;
		break;
	default:
		noEntry();
		break;
	}

	if (Any(textureDesc.Usage, TextureFlag::UnorderedAccess))
	{
		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	}
	if (Any(textureDesc.Usage, TextureFlag::RenderTarget))
	{
		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	}
	if (Any(textureDesc.Usage, TextureFlag::DepthStencil))
	{
		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		if (!Any(textureDesc.Usage, TextureFlag::ShaderResource))
		{
			desc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
		}
	}

	return desc;
}

void GraphicsTexture::Create(const TextureDesc& textureDesc)
{
	m_Desc = textureDesc;
	TextureFlag depthAndRt = TextureFlag::RenderTarget | TextureFlag::DepthStencil;
	check(All(textureDesc.Usage, depthAndRt) == false);

	Release();

	SetResourceState(D3D12_RESOURCE_STATE_COMMON);

	D3D12_CLEAR_VALUE* pClearValue{};
	D3D12_CLEAR_VALUE clearValue{};
	clearValue.Format = textureDesc.Format;

	if (Any(textureDesc.Usage, TextureFlag::RenderTarget))
	{
		check(textureDesc.ClearBindingValue.BindingValue == ClearBinding::ClearBindingValue::Color);
		memcpy(clearValue.Color, &textureDesc.ClearBindingValue.Color, sizeof(Color));
		pClearValue = &clearValue;
		SetResourceState(D3D12_RESOURCE_STATE_RENDER_TARGET);
	}
	if (Any(textureDesc.Usage, TextureFlag::DepthStencil))
	{
		check(textureDesc.ClearBindingValue.BindingValue == ClearBinding::ClearBindingValue::DepthStencil);
		clearValue.DepthStencil.Depth = textureDesc.ClearBindingValue.DepthStencil.Depth;
		clearValue.DepthStencil.Stencil = textureDesc.ClearBindingValue.DepthStencil.Stencil;
		pClearValue = &clearValue;
		SetResourceState(D3D12_RESOURCE_STATE_DEPTH_WRITE);
	}

	D3D12_RESOURCE_DESC desc = GetResourceDesc(textureDesc);
	m_pResource = m_pGraphics->CreateResource(desc, GetResourceState(), D3D12_HEAP_TYPE_DEFAULT, pClearValue);

	if (Any(textureDesc.Usage, TextureFlag::ShaderResource))
	{
		CreateSRV(&m_pSrv, TextureSRVDesc(0));
	}
	if (Any(textureDesc.Usage, TextureFlag::UnorderedAccess))
	{
		CreateUAV(&m_pUav, TextureUAVDesc(0));
	}
	if (Any(textureDesc.Usage, TextureFlag::RenderTarget))
	{
		if (m_Rtv.ptr == 0)
		{
			m_Rtv = m_pGraphics->AllocateDescriptor<D3D12_RENDER_TARGET_VIEW_DESC>();
		}

		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
		rtvDesc.Format = textureDesc.Format;
		switch (textureDesc.Dimension)
		{
		case TextureDimension::Texture1D:
			rtvDesc.Texture2D.MipSlice = 0;
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
			break;
		case TextureDimension::Texture1DArray:
			rtvDesc.Texture1DArray.ArraySize = textureDesc.DepthOrArraySize;
			rtvDesc.Texture1DArray.FirstArraySlice = 0;
			rtvDesc.Texture1DArray.MipSlice = 0;
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
			break;
		case TextureDimension::Texture2D:
			if (textureDesc.SampleCount > 1)
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
			if (textureDesc.SampleCount > 1)
			{
				rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
			}
			else
			{
				rtvDesc.Texture2DArray.ArraySize = textureDesc.DepthOrArraySize;
				rtvDesc.Texture2DArray.FirstArraySlice = 0;
				rtvDesc.Texture2DArray.PlaneSlice = 0;
				rtvDesc.Texture2DArray.MipSlice = 0;
				rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
			}
			break;
		case TextureDimension::Texture3D:
			rtvDesc.Texture3D.FirstWSlice = 0;
			rtvDesc.Texture3D.MipSlice = 0;
			rtvDesc.Texture3D.WSize = textureDesc.DepthOrArraySize;
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
			break;
		default:
			break;
		}
		
		m_pGraphics->GetDevice()->CreateRenderTargetView(m_pResource, &rtvDesc, m_Rtv);
	}
	else if (Any(textureDesc.Usage, TextureFlag::DepthStencil))
	{
		if (m_Rtv.ptr == 0)
		{
			m_Rtv = m_pGraphics->AllocateDescriptor<D3D12_DEPTH_STENCIL_VIEW_DESC>();
		}

		if (m_ReadOnlyDsv.ptr == 0)
		{
			m_ReadOnlyDsv = m_pGraphics->AllocateDescriptor<D3D12_DEPTH_STENCIL_VIEW_DESC>();
		}

		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
		dsvDesc.Format = D3D::GetDsvFormat(textureDesc.Format);
		switch (textureDesc.Dimension)
		{
		case TextureDimension::Texture1D:
			dsvDesc.Texture2D.MipSlice = 0;
			dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
			break;
		case TextureDimension::Texture1DArray:
			dsvDesc.Texture1DArray.ArraySize = textureDesc.DepthOrArraySize;
			dsvDesc.Texture1DArray.FirstArraySlice = 0;
			dsvDesc.Texture1DArray.MipSlice = 0;
			dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
			break;
		case TextureDimension::Texture2D:
			dsvDesc.Texture2D.MipSlice = 0;
			dsvDesc.ViewDimension = (textureDesc.SampleCount > 1) ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
			break;
		case TextureDimension::Texture3D:
		case TextureDimension::TextureCube:
		case TextureDimension::TextureCubeArray:
		case TextureDimension::Texture2DArray:
			dsvDesc.Texture2DArray.ArraySize = textureDesc.DepthOrArraySize;
			dsvDesc.Texture2DArray.FirstArraySlice = 0;
			dsvDesc.Texture2DArray.MipSlice = 0;
			dsvDesc.ViewDimension = (textureDesc.SampleCount > 1) ? D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY : D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
			break;
		default:
			break;
		}

		m_pGraphics->GetDevice()->CreateDepthStencilView(m_pResource, &dsvDesc, m_Rtv);

		dsvDesc.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
		m_pGraphics->GetDevice()->CreateDepthStencilView(m_pResource, &dsvDesc, m_ReadOnlyDsv);

		SetName(m_Name.c_str());
	}
}

DXGI_FORMAT GraphicsTexture::GetSrvFormat(DXGI_FORMAT format)
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
	}

	return format;
}

bool GraphicsTexture::Create(CommandContext* pContext, const char* filePath, bool srgb)
{
	Image img;
	if (img.Load(filePath))
	{
		return Create(pContext, img, srgb);
	}

	return false;
}

bool GraphicsTexture::Create(CommandContext* pContext, const Image& img, bool srgb)
{
	TextureDesc desc;
	desc.Width = img.GetWidth();
	desc.Height = img.GetHeight();
	desc.Format = (DXGI_FORMAT)Image::TextureFormatFromCompressionFormat(img.GetFormat(), srgb);
	desc.Mips = img.GetMipLevels();
	desc.Usage = TextureFlag::ShaderResource;
	desc.Dimension = img.IsCubemap() ? TextureDimension::TextureCube : TextureDimension::Texture2D;
	if (D3D::IsBlockCompressFormat(desc.Format))
	{
		desc.Width = Math::Max(desc.Width, 4);
		desc.Height = Math::Max(desc.Height, 4);
	}

	const Image* pImg = &img;
	std::vector<D3D12_SUBRESOURCE_DATA> subresourceData;
	int resourceOffset = 0;
	while (pImg)
	{
		subresourceData.resize(subresourceData.size() + desc.Mips);
		for (int i = 0; i < desc.Mips; i++)
		{
			D3D12_SUBRESOURCE_DATA& data = subresourceData[resourceOffset++];
			MipLevelInfo mipLevelInfo = pImg->GetMipLevelInfo(i);
			data.pData = pImg->GetData(i);
			data.RowPitch = mipLevelInfo.RowSize;
			data.SlicePitch = mipLevelInfo.RowSize * mipLevelInfo.Height;
		}
		pImg = pImg->GetNextImage();
	}

	Create(desc);
	pContext->InitializeTexture(this, subresourceData.data(), 0, (int)subresourceData.size());
	return true;
}

void GraphicsTexture::SetData(CommandContext* pContext, const void* pData)
{
	D3D12_SUBRESOURCE_DATA data;
	data.pData = pData;
	data.RowPitch = D3D::GetFormatRowDataSize(m_Desc.Format, m_Desc.Width);
	data.SlicePitch = data.RowPitch * m_Desc.Height;
	pContext->InitializeTexture(this, &data, 0, 1);
}

void GraphicsTexture::CreateUAV(UnorderedAccessView** pView, const TextureUAVDesc& desc)
{
	if (*pView == nullptr)
	{
		m_Descriptors.push_back(std::make_unique<UnorderedAccessView>(m_pGraphics));
		*pView = static_cast<UnorderedAccessView*>(m_Descriptors.back().get());
	}
	(*pView)->Create(this, desc);
}

void GraphicsTexture::CreateSRV(ShaderResourceView** pView, const TextureSRVDesc& desc)
{
	if (*pView == nullptr)
	{
		m_Descriptors.push_back(std::make_unique<ShaderResourceView>(m_pGraphics));
		*pView = static_cast<ShaderResourceView*>(m_Descriptors.back().get());
	}
	(*pView)->Create(this, desc);
}

void GraphicsTexture::CreateForSwapChain(ID3D12Resource* pTexture)
{
	Release();
	D3D::SetObjectName(pTexture, "Backbuffer");
	m_pResource = pTexture;
	SetResourceState(D3D12_RESOURCE_STATE_PRESENT);

	D3D12_RESOURCE_DESC desc = pTexture->GetDesc();
	m_Desc.Width = (uint32_t)desc.Width;
	m_Desc.Height = (uint32_t)desc.Height;
	m_Desc.Format = desc.Format;
	m_Desc.ClearBindingValue = ClearBinding(Color(0, 0, 0, 1));
	
	if (m_Rtv.ptr == 0)
	{
		m_Rtv = m_pGraphics->AllocateDescriptor<D3D12_RENDER_TARGET_VIEW_DESC>();
	}
	m_pGraphics->GetDevice()->CreateRenderTargetView(pTexture, nullptr, m_Rtv);

	CreateSRV(&m_pSrv, TextureSRVDesc(0));
}