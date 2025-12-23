#pragma once

#include "Graphics/RenderGraph/RenderGraphDefinition.h"

class GraphicsDevice;
class RootSignature;
class PipelineState;
class GraphicsBuffer;
class GraphicsTexture;
class Buffer;
class Camera;
class UnorderedAccessView;
class RGGraph;
struct SceneView;

class TiledForward
{
public:
	TiledForward(GraphicsDevice* pGraphicsDevice);

    void OnResize(int windowWidth, int windowHeight);
    void Execute(RGGraph& graph, const SceneView& inputResource);
	void VisualizeLightDensity(RGGraph& graph, Camera& camera, GraphicsTexture* pTarget, GraphicsTexture* pDepth);

private:
    void SetupPipelines();

	GraphicsDevice* m_pGraphicsDevice;

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
