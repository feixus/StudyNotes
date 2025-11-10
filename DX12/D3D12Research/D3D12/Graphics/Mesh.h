#pragma once
#include "Core/GraphicsBuffer.h"

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

    VertexBufferView GetVertexBuffer() const { return m_VerticesLocation; };
    IndexBufferView GetIndexBuffer() const { return m_IndicesLocation; };
    Buffer* GetSourceBuffer() const;

private:
    int m_Stride{0};
    int m_MaterialId{};
    VertexBufferView m_VerticesLocation;
    IndexBufferView m_IndicesLocation;
    BoundingBox m_Bounds;
    Mesh* m_pParent;
};

struct Material
{
	GraphicsTexture* pDiffuseTexture{nullptr};
	GraphicsTexture* pNormalTexture{nullptr};
	GraphicsTexture* pRoughnessTexture{nullptr};
	GraphicsTexture* pMetallicTexture{nullptr};
    bool IsTransparent{false};
};

class Mesh
{
public:
    bool Load(const char* pFilePath, Graphics* pGraphics, CommandContext* pContext);
    int GetMeshCount() const { return (int)m_Meshes.size();  }
	SubMesh* GetMesh(int index) const { return m_Meshes[index].get(); }
    const Material& GetMaterial(int materialId) const { return m_Materials[materialId]; }

    Buffer* GetBLAS() const { return m_pBLAS.get(); }
    Buffer* GetData() const { return m_pGeometryData.get(); }

private:
    void GenerateBLAS(Graphics* pGraphics, CommandContext* pContext);

    std::vector<std::unique_ptr<SubMesh>> m_Meshes;
    std::vector<Material> m_Materials;
    std::unique_ptr<Buffer> m_pGeometryData;
    std::vector<std::unique_ptr<GraphicsTexture>> m_Textures;
    std::map<StringHash, GraphicsTexture*> m_ExistingTextures;
    std::unique_ptr<Buffer> m_pBLAS;
	std::unique_ptr<Buffer> m_pBLASScratch;
};
