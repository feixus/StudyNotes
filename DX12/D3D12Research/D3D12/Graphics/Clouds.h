#pragma once
#include "GraphicsResource.h"

class ComputePipelineState;
class GraphicsPipelineState;
class RootSignature;
class GraphicsTexture;
class Graphics;
class GraphicsTexture;
class Buffer;

class Clouds : public GraphicsObject
{
public:
	Clouds(Graphics* pGraphics);
	void Initialize();
	void OnSwapchainCreated(int windowWidth, int windowHeight);
	void RenderUI();
	void Render(GraphicsTexture* pSceneTexture, GraphicsTexture* pDepthTexture);

private:
	std::unique_ptr<ComputePipelineState> m_pWorleyNoisePS;
	std::unique_ptr<RootSignature> m_pWorleyNoiseRS;
	std::unique_ptr<GraphicsTexture> m_pWorleyNoiseTexture;

	std::unique_ptr<GraphicsPipelineState> m_pCloudsPS;
	std::unique_ptr<RootSignature> m_pCloudsRS;

	std::unique_ptr<GraphicsTexture> m_pIntermediateColor;
	std::unique_ptr<GraphicsTexture> m_pIntermediateDepth;

	std::unique_ptr<Buffer> m_pQuadVertexBuffer;
};