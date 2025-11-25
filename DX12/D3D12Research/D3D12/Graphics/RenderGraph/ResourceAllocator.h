#pragma once

class GraphicsDevice;
class GraphicsTexture;
struct TextureDesc;


class RGResourceAllocator
{
public:
	RGResourceAllocator(GraphicsDevice* pGraphicsDevice) : m_pGraphicsDevice(pGraphicsDevice) {}

	GraphicsTexture* CreateTexture(const TextureDesc& desc);
	void ReleaseTexture(GraphicsTexture* pTexture);

private:
	GraphicsDevice* m_pGraphicsDevice;
	std::vector<std::unique_ptr<GraphicsTexture>> m_Textures;
	std::vector<GraphicsTexture*> m_TextureCache;
};
