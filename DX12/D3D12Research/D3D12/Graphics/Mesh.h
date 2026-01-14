#pragma once
#include "Core/GraphicsBuffer.h"

class GraphicsTexture;
class CommandContext;
class ShaderResourceView;
class Mesh;

struct SubMesh
{
    void Destroy();

    int PositionsStride{0};
    int MaterialId{0};
    DXGI_FORMAT PositionsFormat{DXGI_FORMAT_R32G32B32_FLOAT};
    ShaderResourceView* pPositionStreamSRV{nullptr};
    ShaderResourceView* pUVStreamSRV{nullptr};
    ShaderResourceView* pNormalStreamSRV{nullptr};
    ShaderResourceView* pIndexSRV{nullptr};
    VertexBufferView PositionStreamLocation;
    VertexBufferView UVStreamLocation;
    VertexBufferView NormalStreamLocation;
    IndexBufferView IndicesLocation;
    BoundingBox Bounds;
    Mesh* pParent{nullptr};

	Buffer* pBLAS{nullptr};
	Buffer* pBLASScratch{nullptr};
};

struct SubMeshInstance
{
	int MeshIndex;
	Matrix Transform;
};

struct Material
{
    Color BaseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
    Color EmissiveFactor{0.0f, 0.0f, 0.0f, 1.0f};
    float MetalnessFactor{1.0f};
    float RoughnessFactor{1.0f};
    float AlphaCutoff{0.5f};
	GraphicsTexture* pDiffuseTexture{nullptr};
	GraphicsTexture* pNormalTexture{nullptr};
	GraphicsTexture* pRoughnessMetalnessTexture{nullptr};
	GraphicsTexture* pEmissiveTexture{nullptr};
	bool IsTransparent;
};

class Mesh
{
public:
    ~Mesh();
	bool Load(const char* pFilePath, GraphicsDevice* pGraphicDevice, CommandContext* pContext, Vector3 scale = Vector3::One);
	bool LoadByCgltf(const char* pFilePath, GraphicsDevice* pGraphicDevice, CommandContext* pContext, float uniformScale = 1);
    int GetMeshCount() const { return (int)m_Meshes.size();  }
	const SubMesh& GetMesh(int index) const { return m_Meshes[index]; }
    const Material& GetMaterial(int materialId) const { return m_Materials[materialId]; }
	const std::vector<SubMeshInstance>& GetMeshInstances() const { return m_MeshInstances; }
    const std::vector<Material>& GetMaterials() const { return m_Materials; }
    const std::vector<SubMesh>& GetMeshes() const { return m_Meshes; }

private:
    void GenerateBLAS(GraphicsDevice* pGraphicDevice, CommandContext* pContext);

    std::vector<Material> m_Materials;
    std::unique_ptr<Buffer> m_pGeometryData;
    std::vector<SubMesh> m_Meshes;
	std::vector<SubMeshInstance> m_MeshInstances;
    std::vector<std::unique_ptr<GraphicsTexture>> m_Textures;
};
