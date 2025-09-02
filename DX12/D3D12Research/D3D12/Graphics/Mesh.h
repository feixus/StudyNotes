#pragma once

class Graphics;
class GraphicsBuffer;
class GraphicsTexture;
class Buffer;
class CommandContext;
class CommandContext;

class SubMesh
{
    friend class Mesh;

public:
    ~SubMesh();
    void Draw(CommandContext* pContext) const;
    int GetMaterialId() const { return m_MaterialId; }
    const BoundingBox& GetBounds() const { return m_Bounds; }

    uint32_t GetVertexByteOffset() const { return m_VertexByteOffset; }
    uint32_t GetIndexByteOffset() const { return m_IndexByteOffset; }
    uint32_t GetVertexCount() const { return m_VertexCount; }
    uint32_t GetIndexCount() const { return m_IndexCount; }

private:
    int m_MaterialId{};
    uint32_t m_IndexCount{};
    uint32_t m_VertexCount{};
    uint32_t m_IndexOffset{};
    uint32_t m_VertexOffset{};
    uint32_t m_VertexByteOffset{};
    uint32_t m_IndexByteOffset{};
    BoundingBox m_Bounds;
    Mesh* m_pParent;
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

	Buffer* GetVertexBuffer() const { return m_pVertexBuffer.get(); }
	Buffer* GetIndexBuffer() const { return m_pIndexBuffer.get(); }

private:
    std::vector<std::unique_ptr<SubMesh>> m_Meshes;
    std::vector<Material> m_Materials;
    std::unique_ptr<Buffer> m_pVertexBuffer;
    std::unique_ptr<Buffer> m_pIndexBuffer;
};
