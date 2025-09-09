#pragma once

class Mesh;
class Graphics;
class RootSignature;
class GraphicsTexture;
class Camera;
class CommandContext;
class Buffer;
class RGGraph;

class RTAO
{
public:
    RTAO(Graphics* pGraphics);

    void Execute(RGGraph& graph, GraphicsTexture* pColor, GraphicsTexture* pDepth, Buffer* pTLAS, Camera& camera);

private:
    void SetupResources(Graphics* pGraphics);
    void SetupPipelines(Graphics* pGraphics);

    std::unique_ptr<RootSignature> m_pRayGenSignature;
    std::unique_ptr<RootSignature> m_pHitSignature;
    std::unique_ptr<RootSignature> m_pMissSignature;
    std::unique_ptr<RootSignature> m_pGlobalRS;

    ComPtr<ID3D12StateObject> m_pStateObject;
};