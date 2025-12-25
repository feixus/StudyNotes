#pragma once
#include "Graphics/RenderGraph/RenderGraphDefinition.h"

class PipelineState;
class RootSignature;
class Buffer;
class GraphicsTexture;
class Camera;
class CommandSignature;
class GraphicsDevice;
class UnorderedAccessView;
class RGGraph;
struct SceneView;

class ClusteredForward
{
public:
    ClusteredForward(GraphicsDevice* pGraphicsDevice);
    ~ClusteredForward() = default;

    void OnResize(int windowWidth, int windowHeight);

    void Execute(RGGraph& graph, const SceneView& inputResource);
    void VisualizeLightDensity(RGGraph& graph, Camera& camera, GraphicsTexture* pTarget, GraphicsTexture* pDepth);
    
private:
    void SetupPipelines();

    GraphicsDevice* m_pGraphicsDevice{nullptr};

	/* based on screen resolutions(eg.1080P), depth complexity(16~32 slices) and GPU performance tradeoff.
    depth axis is sliced logarithmically or exponentially to account for non-linear depth distribution.
    need adjust for diffrent platform or resolution ... */
	uint32_t m_ClusterCountX{0};
    uint32_t m_ClusterCountY{0};
    uint32_t m_MaxClusters{0};

    std::unique_ptr<GraphicsTexture> m_pHeatMapTexture;

    // AABB
    std::unique_ptr<RootSignature> m_pCreateAabbRS;
    PipelineState* m_pCreateAabbPSO{nullptr};
    std::unique_ptr<Buffer> m_pAabbBuffer;

    // light culling
    std::unique_ptr<RootSignature> m_pLightCullingRS;
    PipelineState* m_pLightCullingPSO{nullptr};
    std::unique_ptr<CommandSignature> m_pLightCullingCommandSignature;;
    std::unique_ptr<Buffer> m_pLightIndexGrid;
    std::unique_ptr<Buffer> m_pLightGrid;
    UnorderedAccessView* m_pLightGridRawUAV{nullptr};

    // lighting
    std::unique_ptr<RootSignature> m_pDiffuseRS;
	PipelineState* m_pDiffusePSO{nullptr};
	PipelineState* m_pDiffuseMaskedPSO{nullptr};
	PipelineState* m_pDiffuseTransparencyPSO{nullptr};

    // cluster debug rendering
    std::unique_ptr<RootSignature> m_pVisualizeLightClustersRS;
    PipelineState* m_pVisualizeLightClustersPSO{nullptr};
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
