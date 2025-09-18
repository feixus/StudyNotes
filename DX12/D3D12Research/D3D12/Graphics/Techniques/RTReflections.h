#pragma once
class Mesh;
class Graphics;
class RootSignature;
class GraphicsTexture;
class Camera;
class CommandContext;
class RGGraph;
class Buffer;
struct SceneData;

class RTReflections
{
public:
    RTReflections(Graphics* pGraphics);
    void Execute(RGGraph& graph, const SceneData& sceneData);

private:
    void SetupResources(Graphics* pGraphics);
    void SetupPipelines(Graphics* pGraphics);

    ComPtr<ID3D12StateObject> m_pRtSO;

    std::unique_ptr<RootSignature> m_pRayGenRS;
    std::unique_ptr<RootSignature> m_pMissRS;
    std::unique_ptr<RootSignature> m_pHitRS;
    std::unique_ptr<RootSignature> m_pGlobalRS;

    std::unique_ptr<GraphicsTexture> m_pTestOutput;
};