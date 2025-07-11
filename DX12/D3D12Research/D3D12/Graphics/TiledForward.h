#pragma once
#include "RenderGraph/RenderGraph.h"

class Graphics;
class RootSignature;
class CommandContext;
class ComputePipelineState;
class GraphicsPipelineState;
class GraphicsBuffer;
class GraphicsTexture;
class Buffer;
class Camera;
class UnorderedAccessView;
struct Batch;
struct LightData;

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
    LightData* pLightData{nullptr};
};

class TiledForward
{
public:
	TiledForward(Graphics* pGraphics);

    void OnSwapchainCreated(int windowWidth, int windowHeight);
    void Execute(RGGraph& graph, const TiledForwardInputResource& inputResource);
    void GetData(Buffer** pLightListOpaque, GraphicsTexture** pLightGridOpaque, Buffer** pLightListTransparent, GraphicsTexture** pLightGridTransparent);

private:
	void SetupResources(Graphics* pGraphics);
    void SetupPipelines(Graphics* pGraphics);

	Graphics* m_pGraphics;

	// light culling
	std::unique_ptr<RootSignature> m_pComputeLightCullRS;
	std::unique_ptr<ComputePipelineState> m_pComputeLightCullPipeline;
	std::unique_ptr<Buffer> m_pLightIndexCounter;
	UnorderedAccessView* m_pLightIndexCounterRawUAV{nullptr};
	std::unique_ptr<Buffer> m_pLightIndexListBufferOpaque;
	std::unique_ptr<GraphicsTexture> m_pLightGridOpaque;
	std::unique_ptr<Buffer> m_pLightIndexListBufferTransparent;
	std::unique_ptr<GraphicsTexture> m_pLightGridTransparent;

	//PBR
	std::unique_ptr<RootSignature> m_pPBRDiffuseRS;
	std::unique_ptr<GraphicsPipelineState> m_pPBRDiffusePSO;
	std::unique_ptr<GraphicsPipelineState> m_pPBRDiffuseAlphaPSO;
};