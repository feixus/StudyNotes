#include "stdafx.h"
#include "ResourceAllocator.h"
#include "Graphics/Core/GraphicsTexture.h"


GraphicsTexture* RGResourceAllocator::CreateTexture(const TextureDesc& desc)
{
	for (size_t i = 0; i < m_TextureCache.size(); i++)
	{
		const TextureDesc& otherDesc = m_TextureCache[i]->GetDesc();
		if (otherDesc.Width == desc.Width &&
			otherDesc.Height == desc.Height &&
			otherDesc.DepthOrArraySize == desc.DepthOrArraySize &&
			otherDesc.Format == desc.Format &&
			otherDesc.Mips == desc.Mips &&
			otherDesc.SampleCount == desc.SampleCount &&
			otherDesc.Usage == desc.Usage &&
			otherDesc.ClearBindingValue.BindingValue == desc.ClearBindingValue.BindingValue)
		{
			std::swap(m_TextureCache[i], m_TextureCache.back());
			GraphicsTexture* pTex = m_TextureCache.back();
			m_TextureCache.pop_back();
			return pTex;
		}
	}

	std::unique_ptr<GraphicsTexture> pTexture = std::make_unique<GraphicsTexture>(m_pGraphics, "Pooled Texture");
	pTexture->Create(desc);
	m_Textures.push_back(std::move(pTexture));
	return m_Textures.back().get();
}

void RGResourceAllocator::ReleaseTexture(GraphicsTexture* pTexture)
{
	m_TextureCache.push_back(pTexture);
}
