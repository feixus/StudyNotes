#include "stdafx.h"
#include "Mesh.h"
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"
#include "GraphicsTexture.h"
#include "GraphicsBuffer.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "Core/Paths.h"

bool Mesh::Load(const char* pFilePath, Graphics* pGraphics, CommandContext* pContext)
{
	Assimp::Importer importer;
	const aiScene* pScene = importer.ReadFile(pFilePath,
		aiProcess_Triangulate |
		aiProcess_ConvertToLeftHanded |
		aiProcess_GenUVCoords |
		aiProcess_CalcTangentSpace);

	for (uint32_t i = 0; i < pScene->mNumMeshes; ++i)
	{
		m_Meshes.push_back(LoadMesh(pScene->mMeshes[i], pGraphics, pContext));
	}

	std::filesystem::path filePath(pFilePath);
	std::filesystem::path basePath = filePath.parent_path();

	auto loadTexture = [&](aiMaterial* pMaterial, aiTextureType type, bool srgb)
	{
		std::unique_ptr<GraphicsTexture> pTex = std::make_unique<GraphicsTexture>(pGraphics);

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
				pTex->Create(pContext, "Resources/textures/dummy_ddn.png", srgb);
				break;
			case aiTextureType_SPECULAR:
				pTex->Create(pContext, "Resources/textures/dummy_specular.png", srgb);
				break;
			case aiTextureType_DIFFUSE:
			default:
				pTex->Create(pContext, "Resources/textures/dummy.png", srgb);
				break;
			}
		}
		return pTex;
	};

	m_Materials.resize(pScene->mNumMaterials);
	for (uint32_t i = 0; i < pScene->mNumMaterials; ++i)
	{
		Material& m = m_Materials[i];
		m.pDiffuseTexture = loadTexture(pScene->mMaterials[i], aiTextureType_DIFFUSE, true);
		m.pNormalTexture = loadTexture(pScene->mMaterials[i], aiTextureType_NORMALS, false);
		m.pSpecularTexture = loadTexture(pScene->mMaterials[i], aiTextureType_SPECULAR, false);
		aiString p;
		m.IsTransparent = pScene->mMaterials[i]->GetTexture(aiTextureType_OPACITY, 0, &p) == aiReturn_SUCCESS;
	}

	return true;
}

std::unique_ptr<SubMesh> Mesh::LoadMesh(aiMesh* pMesh, Graphics* pGraphics, CommandContext* pContext)
{
	struct Vertex
	{
		Vector3 Position;
		Vector2 TexCoord;
		Vector3 Normal;
		Vector3 Tangent;
		Vector3 Bitangent;
	};

	std::vector<Vertex> vertices(pMesh->mNumVertices);
	std::vector<uint32_t> indices(pMesh->mNumFaces * 3);

	for (uint32_t i = 0; i < pMesh->mNumVertices; i++)
	{
		Vertex& vertex = vertices[i];
		vertex.Position = *reinterpret_cast<Vector3*>(&pMesh->mVertices[i]);
		vertex.TexCoord = *reinterpret_cast<Vector2*>(&pMesh->mTextureCoords[0][i]);
		vertex.Normal = *reinterpret_cast<Vector3*>(&pMesh->mNormals[i]);
		if (pMesh->HasTangentsAndBitangents())
		{
			vertex.Tangent = *reinterpret_cast<Vector3*>(&pMesh->mTangents[i]);
			vertex.Bitangent = *reinterpret_cast<Vector3*>(&pMesh->mBitangents[i]);
		}
	}

	for (uint32_t i = 0; i < pMesh->mNumFaces; i++)
	{
		const aiFace& face = pMesh->mFaces[i];
		for (uint32_t j = 0; j < 3; j++)
		{
			assert(face.mNumIndices == 3);
			indices[i * 3 + j] = face.mIndices[j];
		}
	}

	std::unique_ptr<SubMesh> pSubMesh = std::make_unique<SubMesh>();
	BoundingBox::CreateFromPoints(pSubMesh->m_Bounds, vertices.size(), (Vector3*)&vertices[0], sizeof(Vertex));

	{
		uint32_t size = (uint32_t)vertices.size() * sizeof(Vertex);
		pSubMesh->m_pVertexBuffer = std::make_unique<Buffer>(pGraphics);
		pSubMesh->m_pVertexBuffer->Create(BufferDesc::CreateVertexBuffer((uint32_t)vertices.size(), sizeof(Vertex)));
		pSubMesh->m_pVertexBuffer->SetData(pContext, vertices.data(), size);
	}

	{
		uint32_t size = sizeof(uint32_t) * (uint32_t)indices.size();
		pSubMesh->m_IndexCount = (int)indices.size();
		pSubMesh->m_pIndexBuffer = std::make_unique<Buffer>(pGraphics);
		pSubMesh->m_pIndexBuffer->Create(BufferDesc::CreateIndexBuffer((uint32_t)indices.size(), false));
		pSubMesh->m_pIndexBuffer->SetData(pContext, indices.data(), size);
	}

	pSubMesh->m_MaterialId = pMesh->mMaterialIndex;

	return pSubMesh;
}

SubMesh::~SubMesh()
{
}

void SubMesh::Draw(CommandContext* pContext) const
{
	pContext->SetVertexBuffer(m_pVertexBuffer.get());
	pContext->SetIndexBuffer(m_pIndexBuffer.get());
	pContext->DrawIndexed(m_IndexCount, 0, 0);
}

