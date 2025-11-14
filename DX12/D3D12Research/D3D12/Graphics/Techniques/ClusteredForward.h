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
struct SceneData;

class ClusteredForward
{
public:
    ClusteredForward(Graphics* pGraphics);
    ~ClusteredForward() = default;

    void OnSwapchainCreated(int windowWidth, int windowHeight);

    void Execute(RGGraph& graph, const SceneData& inputResource);
    void VisualizeLightDensity(RGGraph& graph, Camera& camera, GraphicsTexture* pTarget, GraphicsTexture* pDepth);
    
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
    PipelineState* m_pCreateAabbPSO{nullptr};
    std::unique_ptr<Buffer> m_pAabbBuffer;

    // step 2: mark unique clusters
    std::unique_ptr<RootSignature> m_pMarkUniqueClustersRS;
	PipelineState* m_pMarkUniqueClustersOpaquePSO{nullptr};
	PipelineState* m_pMarkUniqueClustersTransparentPSO{nullptr};
    std::unique_ptr<Buffer> m_pUniqueClusterBuffer;
    UnorderedAccessView* m_pUniqueClusterBufferRawUAV{nullptr};

    // step 3: compact cluster list
    std::unique_ptr<RootSignature> m_pCompactClusterListRS;
    PipelineState* m_pCompactClusterListPSO{nullptr};
    std::unique_ptr<Buffer> m_pCompactedClusterBuffer;
    UnorderedAccessView* m_pCompactedClusterBufferRawUAV{nullptr};

    // step 4: update Indirect Dispatch Buffer
    std::unique_ptr<RootSignature> m_pUpdateIndirectArgumentsRS;
	PipelineState* m_pUpdateIndirectArgumentsPSO{nullptr};
    std::unique_ptr<Buffer> m_pIndirectArguments;

    // step 5: light culling
    std::unique_ptr<RootSignature> m_pLightCullingRS;
    PipelineState* m_pLightCullingPSO{nullptr};
    std::unique_ptr<CommandSignature> m_pLightCullingCommandSignature;;
    std::unique_ptr<Buffer> m_pLightIndexCounter;
    std::unique_ptr<Buffer> m_pLightIndexGrid;
    std::unique_ptr<Buffer> m_pLightGrid;
    UnorderedAccessView* m_pLightGridRawUAV{nullptr};

    // step 6: lighting
    std::unique_ptr<RootSignature> m_pDiffuseRS;
	PipelineState* m_pDiffusePSO{nullptr};
	PipelineState* m_pDiffuseTransparencyPSO{nullptr};

    // cluster debug rendering
    std::unique_ptr<RootSignature> m_pDebugClusterRS;
    PipelineState* m_pDebugClusterPSO{nullptr};
    std::unique_ptr<Buffer> m_pDebugCompactedClusterBuffer;
    std::unique_ptr<Buffer> m_pDebugLightGrid;
    Matrix m_DebugClusterViewMatrix;
    bool m_DidCopyDebugClusterData{false};

    // visualize light count
	std::unique_ptr<RootSignature> m_pVisualizeLightsRS;
	PipelineState* m_pVisualizeLightsPSO{nullptr};
	std::unique_ptr<GraphicsTexture> m_pVisualizeIntermediateTexture;

    // volumetric fog
    std::unique_ptr<GraphicsTexture> m_pLightScatteringVolume[2];
    std::unique_ptr<GraphicsTexture> m_pFinalVolumeFog;
    std::unique_ptr<RootSignature> m_pVolumetricLightingRS;
    PipelineState* m_pInjectVolumeLightPSO{nullptr};
    PipelineState* m_pAccumulateVolumeLightPSO{nullptr};

    bool m_ViewportDirty{true};
};
