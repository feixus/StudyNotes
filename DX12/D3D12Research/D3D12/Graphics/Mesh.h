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

    D3D12_GPU_VIRTUAL_ADDRESS GetVerticesLocation() const { return m_VerticesLocation; }
    D3D12_GPU_VIRTUAL_ADDRESS GetIndicesLocation() const { return m_IndicesLocation; }
    uint32_t GetVertexCount() const { return m_VertexCount; }
    uint32_t GetIndexCount() const { return m_IndexCount; }
    int GetStride() const { return m_Stride; }

private:
    int m_Stride{0};
    int m_MaterialId{};
    uint32_t m_IndexCount{};
    uint32_t m_VertexCount{};
    D3D12_GPU_VIRTUAL_ADDRESS m_VerticesLocation;
    D3D12_GPU_VIRTUAL_ADDRESS m_IndicesLocation;
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

    Buffer* GetData() const { return m_pGeometryData.get(); }

private:
    std::vector<std::unique_ptr<SubMesh>> m_Meshes;
    std::vector<Material> m_Materials;
    std::unique_ptr<Buffer> m_pGeometryData;
};
