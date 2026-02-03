#pragma once

class GraphicsDevice;
class GraphicsBuffer;
class GraphicsTexture;
class CommandContext;
class RootSignature;
class PipelineState;
class RGGraph;
class Camera;

class GpuParticles
{
public:
    GpuParticles(GraphicsDevice* pGraphicsDevice);
    ~GpuParticles() = default;

    void Simulate(RGGraph& graph, GraphicsTexture* pSourceDepth, const Camera& camera);
    void Render(RGGraph& graph, GraphicsTexture* pTarget, GraphicsTexture* pDepth, const Camera& camera);

private:
    GraphicsDevice* m_pGraphicsDevice;

    std::unique_ptr<GraphicsBuffer> m_pAliveList1;
    std::unique_ptr<GraphicsBuffer> m_pAliveList2;
    std::unique_ptr<GraphicsBuffer> m_pDeadList;
    std::unique_ptr<GraphicsBuffer> m_pParticleBuffer;
    std::unique_ptr<GraphicsBuffer> m_pCounterBuffer;
    
    PipelineState* m_pPrepareArgumentsPSO{nullptr};

    PipelineState* m_pEmitPSO{nullptr};
    std::unique_ptr<GraphicsBuffer> m_pEmitArguments;

    std::unique_ptr<RootSignature> m_pSimulateRS;
    PipelineState* m_pSimulatePSO{nullptr};
    std::unique_ptr<GraphicsBuffer> m_pSimulateArguments;

    PipelineState* m_pSimulateEndPSO{nullptr};
    std::unique_ptr<GraphicsBuffer> m_pDrawArguments;

    std::unique_ptr<RootSignature> m_pParticleRenderRS;
    PipelineState* m_pParticleRenderPSO{nullptr};

    float m_ParticlesToSpawn{0};
};
