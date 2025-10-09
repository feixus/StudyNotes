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
	m_pGeometryData->Create(BufferDesc::CreateBuffer(vertexCount * sizeof(Vertex) + indexCount * sizeof(uint32_t), BufferFlag::ShaderResource | BufferFlag::ByteAddress));

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

	m_Textures.push_back(std::make_unique<GraphicsTexture>(pGraphics, "Dummy White"));
	m_Textures.back()->Create(pContext, "Resources/textures/dummy.dds", true);
	m_Textures.push_back(std::make_unique<GraphicsTexture>(pGraphics, "Dummy Black"));
	m_Textures.back()->Create(pContext, "Resources/textures/dummy_black.dds", true);
	m_Textures.push_back(std::make_unique<GraphicsTexture>(pGraphics, "Dummy Normal"));
	m_Textures.back()->Create(pContext, "Resources/textures/dummy_ddn.dds", true);
	// m_Textures.push_back(std::make_unique<GraphicsTexture>(pGraphics, "Dummy Gray"));
	// m_Textures.back()->Create(pContext, "Resources/textures/dummy_gray.dds", true);

	std::filesystem::path filePath(pFilePath);
	std::filesystem::path basePath = filePath.parent_path();

	auto loadTexture = [&](const aiMaterial* pMaterial, aiTextureType type, bool srgb)
	{
		aiString path;
		aiReturn ret = pMaterial->GetTexture(type, 0, &path);
		bool success = ret == aiReturn_SUCCESS;

		if (!success)
		{
			switch (type)
			{
			case aiTextureType_SPECULAR: return m_Textures[1].get();
			case aiTextureType_NORMALS: return m_Textures[2].get();
			// case aiTextureType_SHININESS: return m_Textures[3].get();
			case aiTextureType_AMBIENT: return m_Textures[1].get();
			default: return m_Textures[0].get();
			}
		}

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

		std::unique_ptr<GraphicsTexture> pTex = std::make_unique<GraphicsTexture>(pGraphics, "Material Texture");
		success = pTex->Create(pContext, pathStr.c_str(), srgb);
		if (success)
		{
			m_Textures.push_back(std::move(pTex));
			m_ExistingTextures[pathHash] = m_Textures.back().get();
			return m_Textures.back().get();
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
		m.pRoughnessTexture = loadTexture(pMaterial, aiTextureType_SPECULAR, false);
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

