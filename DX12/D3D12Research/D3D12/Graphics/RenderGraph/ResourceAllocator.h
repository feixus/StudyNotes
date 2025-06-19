#pragma once

class Graphics;
class GraphicsTexture;
struct TextureDesc;


class RGResourceAllocator
{
public:
	RGResourceAllocator(Graphics* pGraphics) : m_pGraphics(pGraphics) {}

	GraphicsTexture* CreateTexture(const TextureDesc& desc);
	void ReleaseTexture(GraphicsTexture* pTexture);

private:
	Graphics* m_pGraphics;
	std::vector<std::unique_ptr<GraphicsTexture>> m_Textures;
	std::vector<GraphicsTexture*> m_TextureCache;
};
