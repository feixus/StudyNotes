#include "stdafx.h"
#include "Mesh.h"
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"
#include "GraphicsResource.h"
#include "CommandContext.h"
#include "Graphics.h"

bool Mesh::Load(const char* pFilePath, Graphics* pGraphics, GraphicsCommandContext* pContext)
{
	Assimp::Importer importer;
	const aiScene* pScene = importer.ReadFile(pFilePath,
		aiProcess_Triangulate |
		aiProcess_ConvertToLeftHanded |
		aiProcess_GenUVCoords |
		aiProcess_CalcTangentSpace);

	for (uint32_t i = 0; i < pScene->mNumMeshes; ++i)
	{
		m_Meshes.push_back(LoadMesh(pScene->mMeshes[i], pGraphics->GetDevice(), pContext));
		pContext->ExecuteAndReset(true);
	}

	std::filesystem::path filePath(pFilePath);
	std::filesystem::path dirPath = filePath.parent_path();

	auto loadTexture = [pGraphics, pContext](std::filesystem::path basePath, aiMaterial* pMaterial, aiTextureType type)
	{
		std::unique_ptr<GraphicsTexture> pTex = std::make_unique<GraphicsTexture>();

		aiString path;
		aiReturn ret = pMaterial->GetTexture(type, 0, &path);
		if (ret == aiReturn_SUCCESS)
		{ 
			std::filesystem::path texturePath = path.C_Str();
			if (texturePath.is_absolute() || texturePath.has_root_path())
			{
				texturePath = texturePath.relative_path();
			}
			texturePath = basePath / texturePath;
			pTex->Create(pGraphics, pContext, texturePath.string().c_str(), TextureUsage::ShaderResource);
		}
		else
		{
			switch (type)
			{
			case aiTextureType_NORMALS:
				pTex->Create(pGraphics, pContext, "Resources/textures/dummy_ddn.png", TextureUsage::ShaderResource);
				break;
			case aiTextureType_SPECULAR:
				pTex->Create(pGraphics, pContext, "Resources/textures/dummy_specular.png", TextureUsage::ShaderResource);
				break;
			case aiTextureType_DIFFUSE:
			default:
				pTex->Create(pGraphics, pContext, "Resources/textures/dummy.png", TextureUsage::ShaderResource);
				break;
			}
		}
		return pTex;
	};

	m_Materials.resize(pScene->mNumMaterials);
	for (uint32_t i = 0; i < pScene->mNumMaterials; ++i)
	{
		Material& m = m_Materials[i];
		m.pDiffuseTexture = loadTexture(dirPath, pScene->mMaterials[i], aiTextureType_DIFFUSE);
		m.pNormalTexture = loadTexture(dirPath, pScene->mMaterials[i], aiTextureType_NORMALS);
		m.pSpecularTexture = loadTexture(dirPath, pScene->mMaterials[i], aiTextureType_SPECULAR);
		aiString p;
		m.IsTransparent = pScene->mMaterials[i]->GetTexture(aiTextureType_OPACITY, 0, &p) == aiReturn_SUCCESS;
		pContext->ExecuteAndReset(true);
	}

	return true;
}

std::unique_ptr<SubMesh> Mesh::LoadMesh(aiMesh* pMesh, ID3D12Device* pDevice, GraphicsCommandContext* pContext)
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
	{
		uint32_t size = (uint32_t)vertices.size() * sizeof(Vertex);
		pSubMesh->m_pVertexBuffer = std::make_unique<GraphicsBuffer>();
		pSubMesh->m_pVertexBuffer->Create(pDevice, size, false);
		pSubMesh->m_pVertexBuffer->SetData(pContext, vertices.data(), size);

		pSubMesh->m_VertexBufferView.BufferLocation = pSubMesh->m_pVertexBuffer->GetGpuHandle();
		pSubMesh->m_VertexBufferView.SizeInBytes = size;
		pSubMesh->m_VertexBufferView.StrideInBytes = sizeof(Vertex);
	}

	{
		uint32_t size = sizeof(uint32_t) * (uint32_t)indices.size();
		pSubMesh->m_IndexCount = (int)indices.size();
		pSubMesh->m_pIndexBuffer = std::make_unique<GraphicsBuffer>();
		pSubMesh->m_pIndexBuffer->Create(pDevice, size, false);
		pSubMesh->m_pIndexBuffer->SetData(pContext, indices.data(), size);

		pSubMesh->m_IndexBufferView.BufferLocation = pSubMesh->m_pIndexBuffer->GetGpuHandle();
		pSubMesh->m_IndexBufferView.SizeInBytes = size;
		pSubMesh->m_IndexBufferView.Format = DXGI_FORMAT_R32_UINT;
	}

	pSubMesh->m_MaterialId = pMesh->mMaterialIndex;

	return pSubMesh;
}

void SubMesh::Draw(GraphicsCommandContext* pContext)
{
	pContext->SetVertexBuffer(m_VertexBufferView);
	pContext->SetIndexBuffer(m_IndexBufferView);
	pContext->DrawIndexed(m_IndexCount, 0, 0);
}

