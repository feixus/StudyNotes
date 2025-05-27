#pragma once
#include "Graphics/Graphics.h"
#include "Graphics/Light.h"

class ComputePipelineState;
class GraphicsPipelineState;
class RootSignature;
class StructuredBuffer;
class GraphicsTexture;
class ByteAddressBuffer;

struct ClusteredForwardInputResource
{
    GraphicsTexture2D* pDepthPrepassBuffer;
    GraphicsTexture2D* pRenderTarget;
    const std::vector<Batch>* pOpaqueBatches;
    const std::vector<Batch>* pTransparentBatches;
    const std::vector<Light>* pLights;
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

	/* based on screen resolutions(eg.1080P), depth complexity(16~32 slices) and GPU performance tradeoff.
    depth axis is sliced logarithmically or exponentially to account for non-linear depth distribution.
    need adjust for diffrent platform or resolution ... */
	uint32_t m_ClusterCountX{0};
    uint32_t m_ClusterCountY{0};
    uint32_t m_ClusterCountZ{0};
    uint32_t m_MaxClusters{0};

    std::unique_ptr<GraphicsTexture2D> m_pHeatMapTexture;

    // step 1: AABB
    std::unique_ptr<RootSignature> m_pCreateAabbRS;
    std::unique_ptr<ComputePipelineState> m_pCreateAabbPSO;
    std::unique_ptr<StructuredBuffer> m_pAabbBuffer;

    // step 2: mark unique clusters
    std::unique_ptr<RootSignature> m_pMarkUniqueClustersRS;
	std::unique_ptr<GraphicsPipelineState> m_pMarkUniqueClustersOpaquePSO;
	std::unique_ptr<GraphicsPipelineState> m_pMarkUniqueClustersTransparentPSO;
    std::unique_ptr<StructuredBuffer> m_pUniqueClusterBuffer;

    // step 3: compact cluster list
    std::unique_ptr<RootSignature> m_pCompactClusterListRS;
    std::unique_ptr<ComputePipelineState> m_pCompactClusterListPSO;
    std::unique_ptr<StructuredBuffer> m_pActiveClusterListBuffer;

    // step 4: update Indirect Dispatch Buffer
    std::unique_ptr<RootSignature> m_pUpdateIndirectArgumentsRS;
	std::unique_ptr<ComputePipelineState> m_pUpdateIndirectArgumentsPSO;
    std::unique_ptr<ByteAddressBuffer> m_pIndirectArguments;

    // step 5: light culling
    std::unique_ptr<RootSignature> m_pLightCullingRS;
    std::unique_ptr<ComputePipelineState> m_pLightCullingPSO;
    ComPtr<ID3D12CommandSignature> m_pLightCullingCommandSignature;;
    std::unique_ptr<StructuredBuffer> m_pLights;
    std::unique_ptr<StructuredBuffer> m_pLightIndexCounter;
    std::unique_ptr<StructuredBuffer> m_pLightIndexGrid;
    std::unique_ptr<StructuredBuffer> m_pLightGrid;

    // step 6: lighting
    std::unique_ptr<RootSignature> m_pDiffuseRS;
	std::unique_ptr<GraphicsPipelineState> m_pDiffusePSO;
	std::unique_ptr<GraphicsPipelineState> m_pDiffuseTransparencyPSO;
};
