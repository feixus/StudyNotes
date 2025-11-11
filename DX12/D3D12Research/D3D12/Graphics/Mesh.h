#pragma once
#include "Core/GraphicsBuffer.h"

class Graphics;
class GraphicsBuffer;
class GraphicsTexture;
class Buffer;
class CommandContext;
class CommandContext;
class ShaderResourceView;
class Mesh;

struct Material
{
	GraphicsTexture* pDiffuseTexture{nullptr};
	GraphicsTexture* pNormalTexture{nullptr};
	GraphicsTexture* pRoughnessTexture{nullptr};
	GraphicsTexture* pMetallicTexture{nullptr};
	bool IsTransparent;
};

struct SubMesh
{
    void Destroy();

    int Stride{0};
    int MaterialId{0};
    ShaderResourceView* pVertexSRV{nullptr};
    ShaderResourceView* pIndexSRV{nullptr};
    VertexBufferView VerticesLocation;
    IndexBufferView IndicesLocation;
    BoundingBox Bounds;
    Mesh* pParent{nullptr};
};

class Mesh
{
public:
    ~Mesh();
    bool Load(const char* pFilePath, Graphics* pGraphics, CommandContext* pContext);
    int GetMeshCount() const { return (int)m_Meshes.size();  }
	const SubMesh& GetMesh(int index) const { return m_Meshes[index]; }
    const Material& GetMaterial(int materialId) const { return m_Materials[materialId]; }

    Buffer* GetBLAS() const { return m_pBLAS.get(); }
    Buffer* GetData() const { return m_pGeometryData.get(); }

private:
    void GenerateBLAS(Graphics* pGraphics, CommandContext* pContext);

    std::vector<Material> m_Materials;
    std::unique_ptr<Buffer> m_pGeometryData;
    std::vector<SubMesh> m_Meshes;
    std::vector<std::unique_ptr<GraphicsTexture>> m_Textures;
    std::map<StringHash, GraphicsTexture*> m_ExistingTextures;
    std::unique_ptr<Buffer> m_pBLAS;
	std::unique_ptr<Buffer> m_pBLASScratch;
};
