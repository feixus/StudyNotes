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

Mesh::~Mesh()
{
	for (SubMesh& subMesh : m_Meshes)
	{
		subMesh.Destroy();
	}
}

bool Mesh::Load(const char* pFilePath, Graphics* pGraphics, CommandContext* pContext)
{
	Assimp::Importer importer;
	const aiScene* pScene = importer.ReadFile(pFilePath,
		aiProcess_Triangulate |
		aiProcess_ConvertToLeftHanded |
		aiProcess_GenUVCoords |
		aiProcess_CalcTangentSpace |
		aiProcess_GlobalScale);

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

	// SubAllocated buffers need to be 16 byte aligned.
	static constexpr uint64_t sBufferAlignment = 16;
	uint64_t bufferSize = vertexCount * sizeof(Vertex) + indexCount * sizeof(uint32_t) + pScene->mNumMeshes * sBufferAlignment;

	m_pGeometryData = std::make_unique<Buffer>(pGraphics, "Mesh VertexBuffer");
	m_pGeometryData->Create(BufferDesc::CreateBuffer(bufferSize, BufferFlag::ShaderResource | BufferFlag::ByteAddress));

	pContext->InsertResourceBarrier(m_pGeometryData.get(), D3D12_RESOURCE_STATE_COPY_DEST);

	uint64_t dataOffset = 0;
	auto CopyData = [this, &dataOffset, &pContext](void* pSource, uint64_t size)
	{
		m_pGeometryData->SetData(pContext, pSource, size, dataOffset);
		dataOffset += size;
		dataOffset = Math::AlignUp<uint64_t>(dataOffset, sBufferAlignment);
	};

	std::queue<aiNode*> nodesToProcess;
	nodesToProcess.push(pScene->mRootNode);
	while (!nodesToProcess.empty())
	{
		aiNode* pNode = nodesToProcess.front();
		nodesToProcess.pop();
		SubMeshInstance newNode;
		aiMatrix4x4 t = pNode->mTransformation;
		if (pNode->mParent)
		{
			t = pNode->mParent->mTransformation * t;
		}
		newNode.Transform = Matrix(&t.a1).Transpose();
		for (uint32_t i = 0; i < pNode->mNumMeshes; ++i)
		{
			newNode.MeshIndex = pNode->mMeshes[i];
			m_MeshInstances.push_back(newNode);
		}
		for (uint32_t i = 0; i < pNode->mNumChildren; ++i)
		{
			nodesToProcess.push(pNode->mChildren[i]);
		}
	}

	for (uint32_t i = 0; i < pScene->mNumMeshes; i++)
	{
		const aiMesh* pMesh = pScene->mMeshes[i];
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

		SubMesh subMesh;
		BoundingBox::CreateFromPoints(subMesh.Bounds, vertices.size(), (Vector3*)&vertices[0], sizeof(Vertex));
		subMesh.MaterialId = pMesh->mMaterialIndex;

		VertexBufferView vbv(m_pGeometryData->GetGpuHandle() + dataOffset, (uint32_t)vertices.size(), sizeof(Vertex));
		subMesh.VerticesLocation = vbv;
		subMesh.pVertexSRV = new ShaderResourceView();
		subMesh.pVertexSRV->Create(m_pGeometryData.get(), BufferSRVDesc(DXGI_FORMAT_UNKNOWN, true, (uint32_t)dataOffset, (uint32_t)vertices.size() * sizeof(Vertex)));
		CopyData(vertices.data(), sizeof(Vertex) * vertices.size());

		IndexBufferView ibv(m_pGeometryData->GetGpuHandle() + dataOffset, (uint32_t)indices.size(), false);
		subMesh.IndicesLocation = ibv;
		subMesh.pIndexSRV =  new ShaderResourceView();
		subMesh.pIndexSRV->Create(m_pGeometryData.get(), BufferSRVDesc(DXGI_FORMAT_UNKNOWN, true, (uint32_t)dataOffset, (uint32_t)indices.size() * sizeof(uint32_t)));
		CopyData(indices.data(), sizeof(uint32_t) * indices.size());

		subMesh.Stride = sizeof(Vertex);
		subMesh.pParent = this;

		m_Meshes.push_back(subMesh);
	}

	pContext->InsertResourceBarrier(m_pGeometryData.get(), D3D12_RESOURCE_STATE_COMMON);
	pContext->FlushResourceBarriers();

	std::filesystem::path filePath(pFilePath);
	std::filesystem::path basePath = filePath.parent_path();

	std::map<StringHash, GraphicsTexture*> textureMap;

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
			auto it = textureMap.find(pathHash);
			if (it != textureMap.end())
			{
				return it->second;
			}

			std::unique_ptr<GraphicsTexture> pTex = std::make_unique<GraphicsTexture>(pGraphics, pathStr.c_str());
			success = pTex->Create(pContext, pathStr.c_str(), srgb);
			if (success)
			{
				m_Textures.push_back(std::move(pTex));
				textureMap[pathHash] = m_Textures.back().get();
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
		m.pRoughnessMetalnessTexture = loadTexture(pMaterial, aiTextureType_UNKNOWN, false);
		aiString p;
		m.IsTransparent = pMaterial->GetTexture(aiTextureType_OPACITY, 0, &p) == aiReturn_SUCCESS;
	}

	GenerateBLAS(pGraphics, pContext);

	return true;
}

void Mesh::GenerateBLAS(Graphics* pGraphics, CommandContext* pContext)
{
	if (!pGraphics->SupportsRaytracing())
	{
		return;
	}

	ID3D12GraphicsCommandList4* pCmd = pContext->GetRaytracingCommandList();

	// bottom level acceleration structure
	for (size_t i = 0; i < GetMeshCount(); i++)
	{
		SubMesh& subMesh = m_Meshes[i];

		D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc{};
		geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
		geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
		geometryDesc.Triangles.IndexBuffer = subMesh.IndicesLocation.Location;
		geometryDesc.Triangles.IndexCount = subMesh.IndicesLocation.Elements;
		geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
		geometryDesc.Triangles.Transform3x4 = 0;
		geometryDesc.Triangles.VertexBuffer.StartAddress = subMesh.VerticesLocation.Location;
		geometryDesc.Triangles.VertexBuffer.StrideInBytes = subMesh.VerticesLocation.Stride;
		geometryDesc.Triangles.VertexCount = subMesh.VerticesLocation.Elements;
		geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS prebuildInfo = {
			.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
			.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
					D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION,
			.NumDescs = 1,
			.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
			.pGeometryDescs = &geometryDesc,
		};

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
		pGraphics->GetRaytracingDevice()->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfo, &info);

		subMesh.pBLASScratch = new Buffer(pGraphics, "BLAS Scratch Buffer");
		subMesh.pBLASScratch->Create(BufferDesc::CreateByteAddress(Math::AlignUp<uint64_t>(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::None));
		subMesh.pBLAS = new Buffer(pGraphics, "BLAS");
		subMesh.pBLAS->Create(BufferDesc::CreateAccelerationStructure(Math::AlignUp<uint64_t>(info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)));

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {
			.DestAccelerationStructureData = subMesh.pBLAS->GetGpuHandle(),
			.Inputs = prebuildInfo,
			.SourceAccelerationStructureData = 0,
			.ScratchAccelerationStructureData = subMesh.pBLASScratch->GetGpuHandle(),
		};

		pCmd->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
		pContext->InsertUavBarrier(subMesh.pBLAS);
		pContext->FlushResourceBarriers();
	}
}

void SubMesh::Destroy()
{
	delete pIndexSRV;
	delete pVertexSRV;

	delete pBLAS;
	delete pBLASScratch;
}
