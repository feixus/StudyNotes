#pragma once

class Graphics;
class GraphicsBuffer;
class GraphicsTexture;
class Buffer;
class CommandContext;
class CommandContext;
struct aiMesh;
class Mesh;

class SubMesh
{
    friend class Mesh;

public:
    ~SubMesh();
    void Draw(CommandContext* pContext) const;
    int GetMaterialId() const { return m_MaterialId; }
    const BoundingBox& GetBounds() const { return m_Bounds; }

    Buffer* GetVertexBuffer() const { return m_pVertexBuffer.get(); }
    Buffer* GetIndexBuffer() const { return m_pIndexBuffer.get(); }

private:
    int m_MaterialId{};
    int m_IndexCount{};
    int m_VertexCount{};
    BoundingBox m_Bounds;
    std::unique_ptr<Buffer> m_pVertexBuffer;
    std::unique_ptr<Buffer> m_pIndexBuffer;
};

struct Material
{
	std::unique_ptr<GraphicsTexture> pDiffuseTexture;
	std::unique_ptr<GraphicsTexture> pNormalTexture;
	std::unique_ptr<GraphicsTexture> pSpecularTexture;
	std::unique_ptr<GraphicsTexture> pAlphaTexture;
    bool IsTransparent{false};
};

class Mesh
{
public:
    bool Load(const char* pFilePath, Graphics* pGraphics, CommandContext* pContext);
    int GetMeshCount() const { return (int)m_Meshes.size();  }
	SubMesh* GetMesh(int index) const { return m_Meshes[index].get(); }
    const Material& GetMaterial(int materialId) const { return m_Materials[materialId]; }

private:
    std::unique_ptr<SubMesh> LoadMesh(aiMesh* pMesh, Graphics* pGraphics, CommandContext* pContext);

    std::vector<std::unique_ptr<SubMesh>> m_Meshes;
    std::vector<Material> m_Materials;
};
