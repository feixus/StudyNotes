#pragma once

class Mesh;
class GraphicsDevice;
class ShaderManager;
class RootSignature;
class GraphicsTexture;
class Camera;
class CommandContext;
class Buffer;
class RGGraph;
class StateObject;
struct SceneData;

class RTAO
{
public:
    RTAO(GraphicsDevice* pGraphicsDevice);

    void Execute(RGGraph& graph, GraphicsTexture* pColor, GraphicsTexture* pDepth, const SceneData& sceneData, Camera& camera);

private:
    void SetupPipelines();

    GraphicsDevice* m_pGraphicsDevice;
    std::unique_ptr<RootSignature> m_pGlobalRS;
    StateObject* m_pStateObject{nullptr};
};
