#pragma once
#include "CBT.h"

class RootSignature;
class PipelineState;
class Buffer;
class GraphicsTexture;
class GraphicsDevice;
class RGGraph;
class CommandSignature;
struct SceneView;

class CBTTessellation
{
public:
    CBTTessellation(GraphicsDevice* pGraphicsDevice);

    void Execute(RGGraph& graph, GraphicsTexture* pRenderTarget, GraphicsTexture* pDepthTexture, const SceneView& sceneView);

private:
    void AllocateCBT();
    void SetupPipelines();
    void CreateResources();

    void DemoCpuCBT();

    GraphicsDevice* m_pDevice;

    CBT m_CBT;
    bool m_IsDirty{true};
    BoundingFrustum m_CachedFrustum;
    Matrix m_CachedViewMatrix;

    std::unique_ptr<GraphicsTexture> m_pHeightmap;

    std::unique_ptr<RootSignature> m_pCBTRS;
    std::unique_ptr<Buffer> m_pCBTBuffer;
    std::unique_ptr<Buffer> m_pCBTIndirectArgs;
	std::unique_ptr<GraphicsTexture> m_pDebugVisualizeTexture;
	PipelineState* m_pCBTIndirectArgsPSO{nullptr};
	PipelineState* m_pCBTSumReductionPSO{nullptr};
	PipelineState* m_pCBTSumReductionFirstPassPSO{nullptr};
	PipelineState* m_pCBTUpdatePSO{nullptr};
	PipelineState* m_pCBTDebugVisualizePSO{nullptr};
	PipelineState* m_pCBTRenderPSO{nullptr};
	PipelineState* m_pCBTRenderMeshShaderPSO{nullptr};
};
