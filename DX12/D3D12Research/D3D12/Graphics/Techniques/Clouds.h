#pragma once

class PipelineState;
class RootSignature;
class GraphicsTexture;
class Buffer;
class Graphics;
class Camera;
class CommandContext;
class RGGraph;
struct Light;

class Clouds
{
public:
	Clouds(Graphics* pGraphics);
	void Initialize(Graphics* pGraphics);
	void Render(RGGraph& graph, GraphicsTexture* pSceneTexture, GraphicsTexture* pDepthTexture, Camera* pCamera, const Light& sunLight);
	GraphicsTexture* GetNoiseTexture() const { return m_pWorleyNoiseTexture.get(); }

private:
	std::unique_ptr<PipelineState> m_pWorleyNoisePS;
	std::unique_ptr<RootSignature> m_pWorleyNoiseRS;
	std::unique_ptr<GraphicsTexture> m_pWorleyNoiseTexture;

	std::unique_ptr<PipelineState> m_pCloudsPS;
	std::unique_ptr<RootSignature> m_pCloudsRS;

	std::unique_ptr<GraphicsTexture> m_pIntermediateColor;
	std::unique_ptr<GraphicsTexture> m_pIntermediateDepth;

	std::unique_ptr<Buffer> m_pQuadVertexBuffer;

	bool m_UpdateNoise{true};
	BoundingBox m_CloudBounds;

	std::unique_ptr<GraphicsTexture> m_pVerticalDensityTexture;
};