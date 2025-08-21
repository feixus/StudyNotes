#pragma once

#include "Graphics/RenderGraph/RenderGraph.h"

class Graphics;
class RootSignature;
class CommandContext;
class PipelineState;
class GraphicsBuffer;
class GraphicsTexture;
class Buffer;
class Camera;
class UnorderedAccessView;
struct Batch;
struct ShadowData;

struct TiledForwardInputResource
{
    RGResourceHandle ResolvedDepthBuffer;
    RGResourceHandle DepthBuffer;
    GraphicsTexture* pRenderTarget{nullptr};
    GraphicsTexture* pShadowMap{nullptr};
    const std::vector<Batch>* pOpaqueBatches{};
    const std::vector<Batch>* pTransparentBatches{};
    Buffer* pLightBuffer{nullptr};
    Camera* pCamera{nullptr};
	ShadowData* pShadowData{nullptr};
};

class TiledForward
{
public:
	TiledForward(Graphics* pGraphics);

    void OnSwapchainCreated(int windowWidth, int windowHeight);
    void Execute(RGGraph& graph, const TiledForwardInputResource& inputResource);

private:
	void SetupResources(Graphics* pGraphics);
    void SetupPipelines(Graphics* pGraphics);

	// light culling
	std::unique_ptr<RootSignature> m_pComputeLightCullRS;
	std::unique_ptr<PipelineState> m_pComputeLightCullPipeline;
	std::unique_ptr<Buffer> m_pLightIndexCounter;
	UnorderedAccessView* m_pLightIndexCounterRawUAV{nullptr};
	std::unique_ptr<Buffer> m_pLightIndexListBufferOpaque;
	std::unique_ptr<GraphicsTexture> m_pLightGridOpaque;
	std::unique_ptr<Buffer> m_pLightIndexListBufferTransparent;
	std::unique_ptr<GraphicsTexture> m_pLightGridTransparent;

	// diffuse
	std::unique_ptr<RootSignature> m_pDiffuseRS;
	std::unique_ptr<PipelineState> m_pDiffusePSO;
	std::unique_ptr<PipelineState> m_pDiffuseAlphaPSO;
	std::unique_ptr<PipelineState> m_pVisualizeDensityPSO;

};