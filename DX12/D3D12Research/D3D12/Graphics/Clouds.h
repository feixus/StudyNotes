#pragma once

class ComputePipelineState;
class GraphicsPipelineState;
class RootSignature;
class GraphicsTexture;
class Graphics;
class GraphicsTexture;
class Buffer;

class Clouds
{
public:
	void Initialize(Graphics* pGraphics);
	void RenderUI();
	void Render(Graphics* pGraphics, GraphicsTexture* pSceneTexture, GraphicsTexture* pDepthTexture);

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