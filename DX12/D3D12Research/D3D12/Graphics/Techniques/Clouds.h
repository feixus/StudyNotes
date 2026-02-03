#pragma once

class PipelineState;
class RootSignature;
class GraphicsTexture;
class GraphicsBuffer;
class ShaderManager;
class Camera;
class CommandContext;
class RGGraph;
struct Light;
class GraphicsDevice;

class Clouds
{
public:
	Clouds(GraphicsDevice* pGraphicsDevice);
	void Initialize(GraphicsDevice* pGraphicsDevice);
	void Render(RGGraph& graph, GraphicsTexture* pSceneTexture, GraphicsTexture* pDepthTexture, Camera* pCamera, const Light& sunLight);
	GraphicsTexture* GetNoiseTexture() const { return m_pWorleyNoiseTexture.get(); }

private:
	PipelineState* m_pWorleyNoisePS{nullptr};
	std::unique_ptr<RootSignature> m_pWorleyNoiseRS;
	std::unique_ptr<GraphicsTexture> m_pWorleyNoiseTexture;

	PipelineState* m_pCloudsPS{nullptr};
	std::unique_ptr<RootSignature> m_pCloudsRS;

	std::unique_ptr<GraphicsTexture> m_pIntermediateColor;
	std::unique_ptr<GraphicsTexture> m_pIntermediateDepth;

	std::unique_ptr<GraphicsBuffer> m_pQuadVertexBuffer;

	bool m_UpdateNoise{true};
	BoundingBox m_CloudBounds;

	std::unique_ptr<GraphicsTexture> m_pVerticalDensityTexture;
};
