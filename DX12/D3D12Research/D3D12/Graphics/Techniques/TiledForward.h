#pragma once

#include "Graphics/RenderGraph/RenderGraphDefinition.h"

class Graphics;
class RootSignature;
class CommandContext;
class PipelineState;
class GraphicsBuffer;
class GraphicsTexture;
class Buffer;
class Camera;
class UnorderedAccessView;
class RGGraph;
struct Batch;
struct ShadowData;
struct SceneData;

class TiledForward
{
public:
	TiledForward(Graphics* pGraphics);

    void OnSwapchainCreated(int windowWidth, int windowHeight);
    void Execute(RGGraph& graph, const SceneData& inputResource);
	void VisualizeLightDensity(RGGraph& graph, Camera& camera, GraphicsTexture* pTarget, GraphicsTexture* pDepth);

private:
	void SetupResources(Graphics* pGraphics);
    void SetupPipelines(Graphics* pGraphics);

	Graphics* m_pGraphics;

	// light culling
	std::unique_ptr<RootSignature> m_pComputeLightCullRS;
	PipelineState* m_pComputeLightCullPipeline{nullptr};
	std::unique_ptr<Buffer> m_pLightIndexCounter;
	UnorderedAccessView* m_pLightIndexCounterRawUAV{nullptr};
	std::unique_ptr<Buffer> m_pLightIndexListBufferOpaque;
	std::unique_ptr<GraphicsTexture> m_pLightGridOpaque;
	std::unique_ptr<Buffer> m_pLightIndexListBufferTransparent;
	std::unique_ptr<GraphicsTexture> m_pLightGridTransparent;

	// diffuse
	std::unique_ptr<RootSignature> m_pDiffuseRS;
	PipelineState* m_pDiffusePSO{nullptr};
	PipelineState* m_pDiffuseAlphaPSO{nullptr};

	// visualize light count
	std::unique_ptr<RootSignature> m_pVisualizeLightsRS;
	PipelineState* m_pVisualizeLightsPSO{nullptr};
	std::unique_ptr<GraphicsTexture> m_pVisualizeIntermediateTexture;
};