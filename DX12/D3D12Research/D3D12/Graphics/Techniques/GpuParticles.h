#pragma once

class Graphics;
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
    GpuParticles(Graphics* pGraphics);
    ~GpuParticles() = default;

    void Simulate(RGGraph& graph, GraphicsTexture* pSourceDepth, const Camera& camera);
    void Render(RGGraph& graph, GraphicsTexture* pTarget, GraphicsTexture* pDepth, const Camera& camera);

private:
    void Initialize(Graphics* pGraphics);

    std::unique_ptr<Buffer> m_pAliveList1;
    std::unique_ptr<Buffer> m_pAliveList2;
    std::unique_ptr<Buffer> m_pDeadList;
    std::unique_ptr<Buffer> m_pParticleBuffer;
    std::unique_ptr<Buffer> m_pCounterBuffer;
    
    std::unique_ptr<PipelineState> m_pPrepareArgumentsPSO;

    std::unique_ptr<PipelineState> m_pEmitPSO;
    std::unique_ptr<Buffer> m_pEmitArguments;

    std::unique_ptr<RootSignature> m_pSimulateRS;
    std::unique_ptr<PipelineState> m_pSimulatePSO;
    std::unique_ptr<Buffer> m_pSimulateArguments;

    std::unique_ptr<PipelineState> m_pSimulateEndPSO;
    std::unique_ptr<Buffer> m_pDrawArguments;

    std::unique_ptr<CommandSignature> m_pSimpleDispatchCommandSignature;
    std::unique_ptr<CommandSignature> m_pSimpleDrawCommandSignature;

    std::unique_ptr<RootSignature> m_pParticleRenderRS;
    std::unique_ptr<PipelineState> m_pParticleRenderPSO;

    float m_ParticlesToSpawn{0};
};