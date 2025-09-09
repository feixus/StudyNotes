#include "stdafx.h"
#include "Mesh.h"
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"
#include "Core/Paths.h"
#include "Graphics/Core/GraphicsTexture.h"
#include "Graphics/Core/GraphicsBuffer.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/Graphics.h"

bool Mesh::Load(const char* pFilePath, Graphics* pGraphics, CommandContext* pContext)
{
	Assimp::Importer importer;
	const aiScene* pScene = importer.ReadFile(pFilePath,
		aiProcess_Triangulate |
		aiProcess_ConvertToLeftHanded |
		aiProcess_GenUVCoords |
		aiProcess_CalcTangentSpace);

	uint32_t vertexCount = 0;
	uint32_t indexCount = 0;
	for (uint32_t i = 0; i < pScene->mNumMeshes; ++i)
	{
		vertexCount += pScene->mMeshes[i]->mNumVertices;
		indexCount += pScene->mMeshes[i]->mNumFaces * 3;
	}

	struct Vertex
	{
		Vector3 Position;
		Vector2 TexCoord;
		Vector3 Normal;
		Vector3 Tangent;
		Vector3 Bitangent;
	};

	m_pGeometryData = std::make_unique<Buffer>(pGraphics, "Mesh VertexBuffer");
	m_pGeometryData->Create(BufferDesc::CreateBuffer(vertexCount * sizeof(Vertex) + indexCount * sizeof(uint32_t)));

	uint32_t dataOffset = 0;
	for (uint32_t i = 0; i < pScene->mNumMeshes; i++)
	{
		const aiMesh* pMesh = pScene->mMeshes[i];
		std::unique_ptr<SubMesh> pSubMesh = std::make_unique<SubMesh>();
		std::vector<Vertex> vertices(pMesh->mNumVertices);
		std::vector<uint32_t> indices(pMesh->mNumFaces * 3);

		for (uint32_t j = 0; j < pMesh->mNumVertices; j++)
		{
			Vertex& vertex = vertices[j];
			vertex.Position = *reinterpret_cast<Vector3*>(&pMesh->mVertices[j]);
			if (pMesh->HasTextureCoords(0))
			{
				vertex.TexCoord = *reinterpret_cast<Vector2*>(&pMesh->mTextureCoords[0][j]);
			}
			vertex.Normal = *reinterpret_cast<Vector3*>(&pMesh->mNormals[j]);
			if (pMesh->HasTangentsAndBitangents())
			{
				vertex.Tangent = *reinterpret_cast<Vector3*>(&pMesh->mTangents[j]);
				vertex.Bitangent = *reinterpret_cast<Vector3*>(&pMesh->mBitangents[j]);
			}
		}

		for (uint32_t k = 0; k < pMesh->mNumFaces; k++)
		{
			const aiFace& face = pMesh->mFaces[k];
			for (uint32_t m = 0; m < 3; m++)
			{
				check(face.mNumIndices == 3);
				indices[k * 3 + m] = face.mIndices[m];
			}
		}

		BoundingBox::CreateFromPoints(pSubMesh->m_Bounds, vertices.size(), (Vector3*)&vertices[0], sizeof(Vertex));
		pSubMesh->m_MaterialId = pMesh->mMaterialIndex;
		
		pSubMesh->m_VertexCount = (uint32_t)vertices.size();
		pSubMesh->m_VerticesLocation = m_pGeometryData->GetGpuHandle() + dataOffset;
		m_pGeometryData->SetData(pContext, vertices.data(), sizeof(Vertex) * vertices.size(), dataOffset);
		dataOffset += (uint32_t)vertices.size() * sizeof(Vertex);

		pSubMesh->m_IndexCount = (uint32_t)indices.size();
		pSubMesh->m_IndicesLocation = m_pGeometryData->GetGpuHandle() + dataOffset;
		m_pGeometryData->SetData(pContext, indices.data(), sizeof(uint32_t) * indices.size(), dataOffset);
		dataOffset += (uint32_t)indices.size() * sizeof(uint32_t);
		
		pContext->FlushResourceBarriers();

		pSubMesh->m_Stride = sizeof(Vertex);
		pSubMesh->m_pParent = this;

		m_Meshes.push_back(std::move(pSubMesh));
	}

	std::filesystem::path filePath(pFilePath);
	std::filesystem::path basePath = filePath.parent_path();

	auto loadTexture = [&](const aiMaterial* pMaterial, aiTextureType type, bool srgb)
	{
		std::unique_ptr<GraphicsTexture> pTex = std::make_unique<GraphicsTexture>(pGraphics, "Material Texture");

		aiString path;
		aiReturn ret = pMaterial->GetTexture(type, 0, &path);
		bool success = ret == aiReturn_SUCCESS;
		if (success)
		{
			std::filesystem::path texturePath = path.C_Str();
			if (texturePath.is_absolute() || texturePath.has_root_path())
			{
				texturePath = texturePath.relative_path();
			}
			texturePath = basePath / texturePath;
			success = pTex->Create(pContext, texturePath.string().c_str(), srgb);
		}
		
		if (!success)
		{
			switch (type)
			{
			case aiTextureType_NORMALS:
				pTex->Create(pContext, "Resources/textures/dummy_ddn.dds", srgb);
				break;
			case aiTextureType_SPECULAR:
				pTex->Create(pContext, "Resources/textures/dummy_specular.dds", srgb);
				break;
			case aiTextureType_DIFFUSE:
			default:
				pTex->Create(pContext, "Resources/textures/dummy.dds", srgb);
				break;
			}
		}
		return pTex;
	};

	m_Materials.resize(pScene->mNumMaterials);
	for (uint32_t i = 0; i < pScene->mNumMaterials; ++i)
	{
		Material& m = m_Materials[i];
		const aiMaterial* pMaterial = pScene->mMaterials[i];
		m.pDiffuseTexture = loadTexture(pMaterial, aiTextureType_DIFFUSE, true);
		m.pNormalTexture = loadTexture(pMaterial, aiTextureType_NORMALS, false);
		m.pSpecularTexture = loadTexture(pMaterial, aiTextureType_SPECULAR, false);
		aiString p;
		m.IsTransparent = pMaterial->GetTexture(aiTextureType_OPACITY, 0, &p) == aiReturn_SUCCESS;
	}

	return true;
}

SubMesh::~SubMesh()
{}

void SubMesh::Draw(CommandContext* pContext) const
{
	pContext->SetVertexBuffer(VertexBufferView(m_VerticesLocation, m_VertexCount, m_Stride));
	pContext->SetIndexBuffer(IndexBufferView(m_IndicesLocation, m_IndexCount, false));
	pContext->DrawIndexed(m_IndexCount, 0, 0);
}

