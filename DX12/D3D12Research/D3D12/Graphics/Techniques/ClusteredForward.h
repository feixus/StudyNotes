#pragma once
#include "Graphics/RenderGraph/RenderGraphDefinition.h"

class PipelineState;
class RootSignature;
class Buffer;
class GraphicsTexture;
class Camera;
class CommandSignature;
class Graphics;
class CommandContext;
class UnorderedAccessView;
class RGGraph;

struct Batch;
struct ShadowData;

struct ClusteredForwardInputResource
{
    RGResourceHandle DepthBuffer;
    GraphicsTexture* pRenderTarget{nullptr};
    std::vector<std::unique_ptr<GraphicsTexture>>* pShadowMaps{nullptr};
    GraphicsTexture* pAO{nullptr};
    const std::vector<Batch>* pOpaqueBatches{};
    const std::vector<Batch>* pTransparentBatches{};
    Buffer* pLightBuffer{nullptr};
    Camera* pCamera{nullptr};
    ShadowData* pShadowData{nullptr};
};

class ClusteredForward
{
public:
    ClusteredForward(Graphics* pGraphics);
    ~ClusteredForward();

    void OnSwapchainCreated(int windowWidth, int windowHeight);

    void Execute(RGGraph& graph, const ClusteredForwardInputResource& inputResource);

private:
    void SetupResources(Graphics* pGraphics);
    void SetupPipelines(Graphics* pGraphics);

    Graphics* m_pGraphics;

	/* based on screen resolutions(eg.1080P), depth complexity(16~32 slices) and GPU performance tradeoff.
    depth axis is sliced logarithmically or exponentially to account for non-linear depth distribution.
    need adjust for diffrent platform or resolution ... */
	uint32_t m_ClusterCountX{0};
    uint32_t m_ClusterCountY{0};
    uint32_t m_MaxClusters{0};

    std::unique_ptr<GraphicsTexture> m_pHeatMapTexture;

    // step 1: AABB
    std::unique_ptr<RootSignature> m_pCreateAabbRS;
    std::unique_ptr<PipelineState> m_pCreateAabbPSO;
    std::unique_ptr<Buffer> m_pAabbBuffer;

    // step 2: mark unique clusters
    std::unique_ptr<RootSignature> m_pMarkUniqueClustersRS;
	std::unique_ptr<PipelineState> m_pMarkUniqueClustersOpaquePSO;
	std::unique_ptr<PipelineState> m_pMarkUniqueClustersTransparentPSO;
    std::unique_ptr<Buffer> m_pUniqueClusterBuffer;
    UnorderedAccessView* m_pUniqueClusterBufferRawUAV{nullptr};

    // step 3: compact cluster list
    std::unique_ptr<RootSignature> m_pCompactClusterListRS;
    std::unique_ptr<PipelineState> m_pCompactClusterListPSO;
    std::unique_ptr<Buffer> m_pCompactedClusterBuffer;
    UnorderedAccessView* m_pCompactedClusterBufferRawUAV{nullptr};

    // step 4: update Indirect Dispatch Buffer
    std::unique_ptr<RootSignature> m_pUpdateIndirectArgumentsRS;
	std::unique_ptr<PipelineState> m_pUpdateIndirectArgumentsPSO;
    std::unique_ptr<Buffer> m_pIndirectArguments;

    // step 5: light culling
    std::unique_ptr<RootSignature> m_pLightCullingRS;
    std::unique_ptr<PipelineState> m_pLightCullingPSO;
    std::unique_ptr<CommandSignature> m_pLightCullingCommandSignature;;
    std::unique_ptr<Buffer> m_pLightIndexCounter;
    std::unique_ptr<Buffer> m_pLightIndexGrid;
    std::unique_ptr<Buffer> m_pLightGrid;
    UnorderedAccessView* m_pLightGridRawUAV{nullptr};

    // step 6: lighting
    std::unique_ptr<RootSignature> m_pDiffuseRS;
	std::unique_ptr<PipelineState> m_pDiffusePSO;
	std::unique_ptr<PipelineState> m_pDiffuseTransparencyPSO;

    // cluster debug rendering
    std::unique_ptr<RootSignature> m_pDebugClusterRS;
    std::unique_ptr<PipelineState> m_pDebugClusterPSO;
    std::unique_ptr<Buffer> m_pDebugCompactedClusterBuffer;
    std::unique_ptr<Buffer> m_pDebugLightGrid;
    Matrix m_DebugClusterViewMatrix;
    bool m_DidCopyDebugClusterData{false};

    bool m_ViewportDirty{true};
};
