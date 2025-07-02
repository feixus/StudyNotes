#pragma once

class Graphics;
class Buffer;
class Buffer;
class CommandSignature;
class ComputePipelineState;
class GraphicsPipelineState;
class RootSignature;
class CommandContext;

class GpuParticles
{
public:
    GpuParticles(Graphics* pGraphics);
    ~GpuParticles() = default;

    void Initialize();
    void Simulate(CommandContext* pContext);
    void Render();

private:
    Graphics* m_pGraphics{nullptr};

    std::unique_ptr<Buffer> m_pAliveList1;
    std::unique_ptr<Buffer> m_pAliveList2;
    std::unique_ptr<Buffer> m_pDeadList;
    std::unique_ptr<Buffer> m_pParticleBuffer;
    std::unique_ptr<Buffer> m_pCounterBuffer;
    
    std::unique_ptr<RootSignature> m_pPrepareArgumentsRS;
    std::unique_ptr<ComputePipelineState> m_pPrepareArgumentsPSO;

    std::unique_ptr<RootSignature> m_pEmitRS;
    std::unique_ptr<ComputePipelineState> m_pEmitPSO;
    std::unique_ptr<Buffer> m_pEmitArguments;

    std::unique_ptr<RootSignature> m_pSimulateRS;
    std::unique_ptr<ComputePipelineState> m_pSimulatePSO;
    std::unique_ptr<Buffer> m_pSimulateArguments;

    std::unique_ptr<RootSignature> m_pSimulateEndRS;
    std::unique_ptr<ComputePipelineState> m_pSimulateEndPSO;
    std::unique_ptr<Buffer> m_pDrawArguments;

    std::unique_ptr<CommandSignature> m_pSimpleDispatchCommandSignature;
    std::unique_ptr<CommandSignature> m_pSimpleDrawCommandSignature;

    std::unique_ptr<RootSignature> m_pParticleRenderRS;
    std::unique_ptr<GraphicsPipelineState> m_pParticleRenderPSO;
};