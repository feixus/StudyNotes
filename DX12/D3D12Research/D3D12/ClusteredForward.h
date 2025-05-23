#pragma once
#include "Graphics/Graphics.h"

class ComputePipelineState;
class GraphicsPipelineState;
class RootSignature;
class StructuredBuffer;
class GraphicsTexture;

struct ClusteredForwardInputResource
{
    GraphicsTexture2D* pDepthPrepassBuffer;
    GraphicsTexture2D* pRenderTarget;
    const std::vector<Batch>* pOpaqueBatches;
    const std::vector<Batch>* pTransparentBatches;
};

class ClusteredForward
{
public:
    ClusteredForward(Graphics* pGraphics);

    void OnSwapchainCreated(int windowWidth, int windowHeight);

    void Execute(const ClusteredForwardInputResource& inputResource);

private:
    void SetupResources(Graphics* pGraphics);
    void SetupPipelines(Graphics* pGraphics);

    Graphics* m_pGraphics;

    // step 1: AABB
    std::unique_ptr<RootSignature> m_pCreateAabbRS;
    std::unique_ptr<ComputePipelineState> m_pCreateAabbPSO;
    std::unique_ptr<StructuredBuffer> m_pAabbBuffer;

    // step 2: mark unique clusters
    std::unique_ptr<RootSignature> m_pMarkUniqueClustersRS;
    std::unique_ptr<GraphicsPipelineState> m_pMarkUniqueClustersPSO;
    std::unique_ptr<StructuredBuffer> m_pUniqueClusterBuffer;

    std::unique_ptr<GraphicsTexture2D> m_pDebugTexture;

    // step 3: compact cluster list
    std::unique_ptr<RootSignature> m_pCompactClusterListRS;
    std::unique_ptr<ComputePipelineState> m_pCompactClusterListPSO;
    std::unique_ptr<StructuredBuffer> m_pActiveClusterListBuffer;

    // step 4: light culling
    std::unique_ptr<RootSignature> m_pLightCullingRS;
    std::unique_ptr<ComputePipelineState> m_pLightCullingPSO;

    // step 5: lighting
    std::unique_ptr<RootSignature> m_pDiffuseRS;
    std::unique_ptr<GraphicsPipelineState> m_pDiffusePSO;
};
