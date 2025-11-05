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

	uint64_t bufferSize = vertexCount * sizeof(Vertex) + indexCount * sizeof(uint32_t);

	m_pGeometryData = std::make_unique<Buffer>(pGraphics, "Mesh VertexBuffer");
	m_pGeometryData->Create(BufferDesc::CreateBuffer(bufferSize, BufferFlag::ShaderResource | BufferFlag::ByteAddress));

	pContext->InsertResourceBarrier(m_pGeometryData.get(), D3D12_RESOURCE_STATE_COPY_DEST);

	uint64_t dataOffset = 0;
	auto CopyData = [this, &dataOffset, &pContext](void* pSource, uint64_t size)
	{
		m_pGeometryData->SetData(pContext, pSource, size, dataOffset);
		dataOffset += size;
	};

	for (uint32_t i = 0; i < pScene->mNumMeshes; i++)
	{
		const aiMesh* pMesh = pScene->mMeshes[i];
		std::unique_ptr<SubMesh> pSubMesh = std::make_unique<SubMesh>();
		std::vector<Vertex> vertices(pMesh->mNumVertices);
		
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
		
		std::vector<uint32_t> indices(pMesh->mNumFaces * 3);
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
		CopyData(vertices.data(), sizeof(Vertex) * vertices.size());

		pSubMesh->m_IndexCount = (uint32_t)indices.size();
		pSubMesh->m_IndicesLocation = m_pGeometryData->GetGpuHandle() + dataOffset;
		CopyData(indices.data(), sizeof(uint32_t) * indices.size());

		pSubMesh->m_Stride = sizeof(Vertex);
		pSubMesh->m_pParent = this;

		m_Meshes.push_back(std::move(pSubMesh));
	}

	pContext->InsertResourceBarrier(m_pGeometryData.get(), D3D12_RESOURCE_STATE_COMMON);

	std::filesystem::path filePath(pFilePath);
	std::filesystem::path basePath = filePath.parent_path();

	auto loadTexture = [&](const aiMaterial* pMaterial, aiTextureType type, bool srgb)
	{
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

			std::string pathStr = texturePath.string();
			StringHash pathHash = StringHash(pathStr);
			auto it = m_ExistingTextures.find(pathHash);
			if (it != m_ExistingTextures.end())
			{
				return it->second;
			}

			std::unique_ptr<GraphicsTexture> pTex = std::make_unique<GraphicsTexture>(pGraphics, pathStr.c_str());
			success = pTex->Create(pContext, pathStr.c_str(), srgb);
			if (success)
			{
				m_Textures.push_back(std::move(pTex));
				m_ExistingTextures[pathHash] = m_Textures.back().get();
				return m_Textures.back().get();
			}
		}
		return (GraphicsTexture*)nullptr;
	};

	m_Materials.resize(pScene->mNumMaterials);
	for (uint32_t i = 0; i < pScene->mNumMaterials; ++i)
	{
		Material& m = m_Materials[i];
		const aiMaterial* pMaterial = pScene->mMaterials[i];
		m.pDiffuseTexture = loadTexture(pMaterial, aiTextureType_DIFFUSE, true);
		m.pNormalTexture = loadTexture(pMaterial, aiTextureType_NORMALS, false);
		m.pRoughnessTexture = loadTexture(pMaterial, aiTextureType_SHININESS, false);
		m.pMetallicTexture = loadTexture(pMaterial, aiTextureType_AMBIENT, false);
		aiString p;
		m.IsTransparent = pMaterial->GetTexture(aiTextureType_OPACITY, 0, &p) == aiReturn_SUCCESS;
	}

	return true;
}

SubMesh::~SubMesh()
{}

void SubMesh::Draw(CommandContext* pContext) const
{
	pContext->SetVertexBuffer(GetVertexBuffer());
	pContext->SetIndexBuffer(GetIndexBuffer());
	pContext->DrawIndexed(m_IndexCount, 0, 0);
}

VertexBufferView SubMesh::GetVertexBuffer() const
{
	return VertexBufferView(m_VerticesLocation, m_VertexCount, m_Stride);
}

IndexBufferView SubMesh::GetIndexBuffer() const
{
	return IndexBufferView(m_IndicesLocation, m_IndexCount, false);
}

Buffer* SubMesh::GetSourceBuffer() const
{
	return m_pParent->GetData();
}

