#pragma once

class GraphicsDevice;
class ShaderManager;
class RootSignature;
class GraphicsTexture;
class Camera;
class PipelineState;
class RGGraph;

class SSAO
{
public:
	SSAO(GraphicsDevice* pGraphicsDevice);

	void OnResize(int widowWidth, int windowHeight);

	void Execute(RGGraph& graph, GraphicsTexture* pColor, GraphicsTexture* pDepth, Camera& camera);

private:
	void SetupPipelines();

	GraphicsDevice* m_pGraphicsDevice;
	
	std::unique_ptr<GraphicsTexture> m_pAmbientOcclusionIntermediate;

	std::unique_ptr<RootSignature> m_pSSAORS;
	PipelineState* m_pSSAOPSO{nullptr};
	std::unique_ptr<RootSignature> m_pSSAOBlurRS;
	PipelineState* m_pSSAOBlurPSO{nullptr};
};
