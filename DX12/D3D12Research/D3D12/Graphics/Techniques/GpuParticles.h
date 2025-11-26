#pragma once

class DemoApp;
class Buffer;
class Buffer;
class CommandSignature;
class PipelineState;
class PipelineState;
class RootSignature;
class CommandContext;
class RGGraph;
class GraphicsTexture;
class Camera;

class GpuParticles
{
public:
    GpuParticles(DemoApp* pGraphics);
    ~GpuParticles() = default;

    void Simulate(RGGraph& graph, GraphicsTexture* pSourceDepth, const Camera& camera);
    void Render(RGGraph& graph, GraphicsTexture* pTarget, GraphicsTexture* pDepth, const Camera& camera);

private:
    void Initialize(DemoApp* pGraphics);

    std::unique_ptr<Buffer> m_pAliveList1;
    std::unique_ptr<Buffer> m_pAliveList2;
    std::unique_ptr<Buffer> m_pDeadList;
    std::unique_ptr<Buffer> m_pParticleBuffer;
    std::unique_ptr<Buffer> m_pCounterBuffer;
    
    PipelineState* m_pPrepareArgumentsPSO{nullptr};

    PipelineState* m_pEmitPSO{nullptr};
    std::unique_ptr<Buffer> m_pEmitArguments;

    std::unique_ptr<RootSignature> m_pSimulateRS;
    PipelineState* m_pSimulatePSO{nullptr};
    std::unique_ptr<Buffer> m_pSimulateArguments;

    PipelineState* m_pSimulateEndPSO{nullptr};
    std::unique_ptr<Buffer> m_pDrawArguments;

    std::unique_ptr<CommandSignature> m_pSimpleDispatchCommandSignature;
    std::unique_ptr<CommandSignature> m_pSimpleDrawCommandSignature;

    std::unique_ptr<RootSignature> m_pParticleRenderRS;
    PipelineState* m_pParticleRenderPSO{nullptr};

    float m_ParticlesToSpawn{0};
};
