#pragma once

class Mesh;
class Graphics;
class RootSignature;
class GraphicsTexture;
class Camera;
class CommandContext;
class Buffer;
class RGGraph;
class StateObject;

class RTAO
{
public:
    RTAO(Graphics* pGraphics);

    void Execute(RGGraph& graph, GraphicsTexture* pColor, GraphicsTexture* pDepth, Buffer* pTLAS, Camera& camera);

private:
    void SetupResources(Graphics* pGraphics);
    void SetupPipelines(Graphics* pGraphics);

    std::unique_ptr<RootSignature> m_pGlobalRS;
    StateObject* m_pStateObject;
};