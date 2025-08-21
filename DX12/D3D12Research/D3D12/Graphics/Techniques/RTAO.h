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

    void OnSwapchainCreated(int widowWidth, int windowHeight);

    void Execute(RGGraph& graph, GraphicsTexture* pColor, GraphicsTexture* pDepth, Camera& camera);
    void GenerateAccelerationStructure(Graphics* pGraphics, Mesh* pMesh, CommandContext& context);

private:
    void SetupResources(Graphics* pGraphics);
    void SetupPipelines(Graphics* pGraphics);

    Graphics* m_pGraphics{};

    std::unique_ptr<Buffer> m_pBLAS;
    std::unique_ptr<Buffer> m_pTLAS;
    std::unique_ptr<Buffer> m_pBLASScratch;
    std::unique_ptr<Buffer> m_pTLASScratch;
    std::unique_ptr<Buffer> m_pDescriptorsBuffer;

    std::unique_ptr<RootSignature> m_pRayGenSignature;
    std::unique_ptr<RootSignature> m_pHitSignature;
    std::unique_ptr<RootSignature> m_pMissSignature;
    std::unique_ptr<RootSignature> m_pGlobalRS;

    ComPtr<ID3D12StateObject> m_pStateObject;
};