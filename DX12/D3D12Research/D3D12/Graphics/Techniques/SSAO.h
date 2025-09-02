#pragma once

class Graphics;
class RootSignature;
class GraphicsTexture;
class Camera;
class PipelineState;
class RGGraph;

class SSAO
{
public:
	SSAO(Graphics* pGraphics);

	void OnSwapchainCreated(int widowWidth, int windowHeight);

	void Execute(RGGraph& graph, GraphicsTexture* pColor, GraphicsTexture* pDepth, Camera& camera);

private:
	void SetupResources(Graphics* pGraphics);
	void SetupPipelines(Graphics* pGraphics);
	
	std::unique_ptr<GraphicsTexture> m_pAmbientOcclusionIntermediate;

	std::unique_ptr<RootSignature> m_pSSAORS;
	std::unique_ptr<PipelineState> m_pSSAOPSO;
	std::unique_ptr<RootSignature> m_pSSAOBlurRS;
	std::unique_ptr<PipelineState> m_pSSAOBlurPSO;
};