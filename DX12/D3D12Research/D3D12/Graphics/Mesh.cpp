#include "stdafx.h"
#include "Mesh.h"
#include "Core/Paths.h"
#include "Graphics/Core/GraphicsTexture.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/Graphics.h"
#include "Content/Image.h"

#include "Stb/stb_image.h"
#include "Stb/stb_image_write.h"
#include "json/json.hpp"

#define TINYGLTF_NO_INCLUDE_JSON
#define TINYGLTF_NO_EXTERNAL_IMAGE 
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION

#include "tinygltf/tiny_gltf.h"

struct Vertex
{
	Vector3 Position{Vector3::Zero};
	Vector2 TexCoord{Vector2::Zero};
	Vector3 Normal{Vector3::Forward};
	Vector4 Tangent{1, 0, 0, 1};
};

Mesh::~Mesh()
{
	for (SubMesh& subMesh : m_Meshes)
	{
		subMesh.Destroy();
	}
}

namespace GltfCallbacks
{
	bool FileExists(const std::string& abs_filename, void*)
	{
		return Paths::FileExists(abs_filename);
	}

	std::string ExpandFilePath(const std::string& filePath, void*)
	{
		DWORD len = ExpandEnvironmentStringsA(filePath.c_str(), nullptr, 0);
		char* str = new char[len];
		ExpandEnvironmentStringsA(filePath.c_str(), str, len);
		std::string result(str);
		delete[] str;
		return result;
	}

	bool ReadWholeFile(std::vector<unsigned char>* out, std::string* err, const std::string& filePath, void*)
	{
		std::ifstream s(filePath, std::ios::binary | std::ios::ate);
		if (s.fail())
		{
			return false;
		}
		out->resize((size_t)s.tellg());
		s.seekg(0);
		s.read(reinterpret_cast<char*>(out->data()), out->size());
		return true;
	}

	bool WriteWholeFile(std::string* err, const std::string& filePath, const std::vector<unsigned char>& data, void*)
	{
		std::ofstream s(filePath, std::ios::binary);
		if (s.fail())
		{
			return false;
		}
		s.write(reinterpret_cast<const char*>(data.data()), data.size());
		return true;
	}
}

bool Mesh::Load(const char* pFilePath, GraphicsDevice* pGraphicDevice, CommandContext* pContext, float uniformScale)
{
	tinygltf::TinyGLTF loader;
	std::string err, warn;

	tinygltf::FsCallbacks callbacks;
	callbacks.ReadWholeFile = GltfCallbacks::ReadWholeFile;
	callbacks.WriteWholeFile = GltfCallbacks::WriteWholeFile;
	callbacks.FileExists = GltfCallbacks::FileExists;
	callbacks.ExpandFilePath = GltfCallbacks::ExpandFilePath;
	loader.SetFsCallbacks(callbacks);

	std::string extension = Paths::GetFileExtension(pFilePath);
	tinygltf::Model model;
	bool ret = extension == ".gltf" ? loader.LoadASCIIFromFile(&model, &err, &warn, pFilePath) : loader.LoadBinaryFromFile(&model, &err, &warn, pFilePath);
	if (!ret)
	{
		E_LOG(Warning, "GLTF - failed to load '%s': '%s'", pFilePath, err.c_str());
		return false;
	}
	if (warn.length() > 0)
	{
		E_LOG(Warning, "GLTF - warning while loading '%s': '%s'", pFilePath, warn.c_str());
	}

	std::map<StringHash, GraphicsTexture*> textureMap;

	m_Materials.reserve(model.materials.size());
	for (const tinygltf::Material& gltfMaterial : model.materials)
	{
		m_Materials.push_back(Material());
		Material& material = m_Materials.back();

		auto baseColorTexture = gltfMaterial.values.find("baseColorTexture");
		auto metallicRoughnessTexture = gltfMaterial.values.find("metallicRoughnessTexture");
		auto normalTexture = gltfMaterial.additionalValues.find("normalTexture");
		auto emissiveTexture = gltfMaterial.additionalValues.find("emissiveTexture");
		auto baseColorFactor = gltfMaterial.values.find("baseColorFactor");
		auto metallicFactor = gltfMaterial.values.find("metallicFactor");
		auto roughnessFactor = gltfMaterial.values.find("roughnessFactor");
		auto emissiveFactor = gltfMaterial.additionalValues.find("emissiveFactor");
		auto alphaCutoff = gltfMaterial.additionalValues.find("alphaCutoff");
		auto alphaMode = gltfMaterial.additionalValues.find("alphaMode");

		auto RetrieveTexture = [this, &textureMap, &model, pGraphicDevice, pContext, pFilePath](bool isValid, auto gltfParameter, GraphicsTexture** pTarget, bool srgb)
		{
			if (!isValid)
				return;

			int index = gltfParameter->second.TextureIndex();
			if (index < 0 || index >= model.textures.size())
				return;

			const tinygltf::Texture& texture = model.textures[index];
			const tinygltf::Image& image = model.images[texture.source];
			StringHash pathHash = StringHash(image.uri.c_str());
			pathHash.Combine((int)srgb);
			auto it = textureMap.find(pathHash);
			if (it != textureMap.end())
			{
				*pTarget = it->second;
				return;
			}

			std::unique_ptr<GraphicsTexture> pTex = std::make_unique<GraphicsTexture>(pGraphicDevice, image.name.c_str());
			bool success = false;
			if (image.uri.empty())
			{
				Image img;
				img.SetSize(image.width, image.height, 4);
				if (img.SetData(image.image.data()))
				{
					success = pTex->Create(pContext, img, srgb);
				}
			}
			else
			{
				success = pTex->Create(pContext, Paths::Combine(Paths::GetDirectoryPath(pFilePath), image.uri).c_str(), srgb);
			}

			if (success)
			{
				m_Textures.push_back(std::move(pTex));
				textureMap[pathHash] = m_Textures.back().get();
				*pTarget = m_Textures.back().get();
			}
		};

		RetrieveTexture(baseColorTexture != gltfMaterial.values.end(), baseColorTexture, &material.pDiffuseTexture, true);
		RetrieveTexture(metallicRoughnessTexture != gltfMaterial.values.end(), metallicRoughnessTexture, &material.pRoughnessMetalnessTexture, false);
		RetrieveTexture(normalTexture != gltfMaterial.additionalValues.end(), normalTexture, &material.pNormalTexture, false);
		RetrieveTexture(emissiveTexture != gltfMaterial.additionalValues.end(), emissiveTexture, &material.pEmissiveTexture, true);

		if (baseColorFactor != gltfMaterial.values.end())
		{
			const auto& factor = baseColorFactor->second.ColorFactor();
			material.BaseColorFactor = Color((float)factor[0], (float)factor[1], (float)factor[2], (float)factor[3]);
		}
		if (emissiveFactor != gltfMaterial.additionalValues.end())
		{
			const auto& factor = emissiveFactor->second.ColorFactor();
			material.EmissiveFactor = Color((float)factor[0], (float)factor[1], (float)factor[2], 1.0f);
		}
		if (metallicFactor != gltfMaterial.values.end())
		{
			material.MetalnessFactor = (float)metallicFactor->second.Factor();
		}
		if (roughnessFactor != gltfMaterial.values.end())
		{
			material.RoughnessFactor = (float)roughnessFactor->second.Factor();
		}
		if (alphaCutoff != gltfMaterial.additionalValues.end())
		{
			material.AlphaCutoff = (float)alphaCutoff->second.Factor();
		}
		if (alphaMode != gltfMaterial.additionalValues.end())
		{
			material.IsTransparent = alphaMode->second.string_value != "OPAQUE";
		}
	}

	std::vector<uint32_t> indices;
	std::vector<Vertex> vertices;
	struct MeshData
	{
		uint32_t NumIndices{0};
		uint32_t IndexOffset{0};
		uint32_t NumVertices{0};
		uint32_t VertexOffset{0};
		uint32_t MaterialIndex{0};
	};
	std::vector<MeshData> meshDatas;
	std::vector<std::vector<int>> meshToPrimitives;
	int primitiveIndex = 0;

	for (const tinygltf::Mesh& mesh : model.meshes)
	{
		std::vector<int> primitives;
		for (const tinygltf::Primitive& primitive : mesh.primitives)
		{
			primitives.push_back(primitiveIndex++);
			MeshData meshData;
			meshData.MaterialIndex = primitive.material;

			uint32_t vertexOffset = (uint32_t)vertices.size();
			meshData.VertexOffset = vertexOffset;

			// Indices
			{
				const tinygltf::Accessor& accessor = model.accessors[primitive.indices];
				const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
				const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

				int indexStride = accessor.ByteStride(bufferView);
				size_t indexCount = accessor.count;
				uint32_t indexOffset = (uint32_t)indices.size();
				indices.resize(indexOffset + indexCount);

				meshData.IndexOffset = indexOffset;
				meshData.NumIndices = (uint32_t)indexCount;

				const unsigned char* indexData = buffer.data.data() + accessor.byteOffset + bufferView.byteOffset;
				if (indexStride == 4)
				{
					for (size_t i = 0; i < indexCount; ++i)
					{
						indices[indexOffset + i] = ((uint32_t*)indexData)[i];
					}
				}
				else if (indexStride == 2)
				{
					for (size_t i = 0; i < indexCount; ++i)
					{
						indices[indexOffset + i] = ((uint16_t*)indexData)[i];
					}
				}
				else
				{
					noEntry();
				}
			}

			// Vertices
			{
				for (const auto& attribute : primitive.attributes)
				{
					const std::string name = attribute.first;
					int index = attribute.second;

					const tinygltf::Accessor& accessor = model.accessors[index];
					const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
					const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

					int stride = accessor.ByteStride(bufferView);
					const unsigned char* pData = buffer.data.data() + accessor.byteOffset + bufferView.byteOffset;

					if (meshData.NumVertices == 0)
					{
						vertices.resize(vertices.size() + accessor.count);
						meshData.NumVertices = (uint32_t)accessor.count;
					}

					if (name == "POSITION")
					{
						check(stride == sizeof(Vector3));
						const Vector3* pPositions = (Vector3*)pData;
						for (size_t i = 0; i < accessor.count; ++i)
						{
							vertices[vertexOffset + i].Position = pPositions[i];
						}
					}
					else if (name == "NORMAL")
					{
						check(stride == sizeof(Vector3));
						const Vector3* pNormals = (Vector3*)pData;
						for (size_t i = 0; i < accessor.count; ++i)
						{
							vertices[vertexOffset + i].Normal =pNormals[i];
						}
					}
					else if (name == "TANGENT")
					{
						check(stride == sizeof(Vector4));
						const Vector4* pTangents = (Vector4*)pData;
						for (size_t i = 0; i < accessor.count; ++i)
						{
							vertices[vertexOffset + i].Tangent = pTangents[i];
						}
					}
					else if (name == "TEXCOORD_0")
					{
						check(stride == sizeof(Vector2));
						Vector2* pTexCoords = (Vector2*)pData;
						for (size_t i = 0; i < accessor.count; ++i)
						{
							vertices[vertexOffset + i].TexCoord = pTexCoords[i];
						}
					}
				}
			}
			meshDatas.push_back(meshData);
		}
		meshToPrimitives.push_back(primitives);
	}

	static constexpr uint64_t sBufferAlignment = 16;
	uint64_t bufferSize = vertices.size() * sizeof(Vertex) + indices.size() * sizeof(uint32_t) + meshDatas.size() * sBufferAlignment;
	m_pGeometryData = pGraphicDevice->CreateBuffer(BufferDesc::CreateBuffer(bufferSize, BufferFlag::ShaderResource | BufferFlag::ByteAddress), "Mesh GeometryBuffer");
	pContext->InsertResourceBarrier(m_pGeometryData.get(), D3D12_RESOURCE_STATE_COPY_DEST);

	const tinygltf::Scene& gltfScene = model.scenes[Math::Max(0, model.defaultScene)];
	for (size_t i = 0; i < gltfScene.nodes.size(); i++)
	{
		int nodeIndex = gltfScene.nodes[i];
		struct QueuedNode
		{
			int Index;
			Matrix Transform;
		};
		std::queue<QueuedNode> toProcess;
		QueuedNode rootNode;
		rootNode.Index = nodeIndex;
		rootNode.Transform = Matrix::CreateScale(uniformScale);
		toProcess.push(rootNode);
		while (!toProcess.empty())
		{
			QueuedNode currentNode = toProcess.front();
			toProcess.pop();

			const tinygltf::Node& node = model.nodes[currentNode.Index];
			Vector3 scale = node.scale.size() == 0 ? Vector3::One : Vector3((float)node.scale[0], (float)node.scale[1], (float)node.scale[2]);
			Quaternion rotation = node.rotation.size() == 0 ? Quaternion::Identity : Quaternion((float)node.rotation[0], (float)node.rotation[1], (float)node.rotation[2], (float)node.rotation[3]);
			Vector3 translation = node.translation.size() == 0 ? Vector3::Zero : Vector3((float)node.translation[0], (float)node.translation[1], (float)node.translation[2]);

			Matrix matrix = node.matrix.size() == 0 ? Matrix::Identity : Matrix(
				(float)node.matrix[0], (float)node.matrix[4], (float)node.matrix[8], (float)node.matrix[12],
				(float)node.matrix[1], (float)node.matrix[5], (float)node.matrix[9], (float)node.matrix[13],
				(float)node.matrix[2], (float)node.matrix[6], (float)node.matrix[10], (float)node.matrix[14],
				(float)node.matrix[3], (float)node.matrix[7], (float)node.matrix[11], (float)node.matrix[15]
			);

			SubMeshInstance newNode;
			newNode.Transform = currentNode.Transform * matrix * Matrix::CreateFromQuaternion(rotation) * Matrix::CreateScale(scale) * Matrix::CreateTranslation(translation);
			if (node.mesh >= 0)
			{
				for (int primitive : meshToPrimitives[node.mesh])
				{
					newNode.MeshIndex = primitive;
					m_MeshInstances.push_back(newNode);
				}
			}

			for (int child : node.children)
			{
				QueuedNode childNode;
				childNode.Index = child;
				childNode.Transform = newNode.Transform;
				toProcess.push(childNode);
			}
		}
	}

	uint64_t dataOffset = 0;
	auto CopyData = [&](void* pSource, uint64_t size)
	{
		m_pGeometryData->SetData(pContext, pSource, size, dataOffset);
		dataOffset += size;
		dataOffset = Math::AlignUp<uint64_t>(dataOffset, sBufferAlignment);
	};

	for (const MeshData& meshData : meshDatas)
	{
		SubMesh subMesh;
		BoundingBox::CreateFromPoints(subMesh.Bounds, meshData.NumVertices, (Vector3*)&vertices[meshData.VertexOffset], sizeof(Vertex));
		subMesh.MaterialId = meshData.MaterialIndex;

		VertexBufferView vbv(m_pGeometryData->GetGpuHandle() + dataOffset, meshData.NumVertices, sizeof(Vertex));
		subMesh.VerticesLocation = vbv;
		subMesh.pVertexSRV = new ShaderResourceView();
		subMesh.pVertexSRV->Create(m_pGeometryData.get(), BufferSRVDesc(DXGI_FORMAT_UNKNOWN, true, (uint32_t)dataOffset, meshData.NumVertices * sizeof(Vertex)));
		CopyData(&vertices[meshData.VertexOffset], sizeof(Vertex) * meshData.NumVertices);

		IndexBufferView ibv(m_pGeometryData->GetGpuHandle() + dataOffset, meshData.NumIndices, false);
		subMesh.IndicesLocation = ibv;
		subMesh.pIndexSRV = new ShaderResourceView();
		subMesh.pIndexSRV->Create(m_pGeometryData.get(), BufferSRVDesc(DXGI_FORMAT_UNKNOWN, true, (uint32_t)dataOffset, meshData.NumIndices * sizeof(uint32_t)));
		CopyData(&indices[meshData.IndexOffset], sizeof(uint32_t) * meshData.NumIndices);

		subMesh.Stride = sizeof(Vertex);
		subMesh.pParent = this;
		m_Meshes.push_back(subMesh);
	}

	pContext->InsertResourceBarrier(m_pGeometryData.get(), D3D12_RESOURCE_STATE_COMMON);
	pContext->FlushResourceBarriers();

	GenerateBLAS(pGraphicDevice, pContext);
	return true;
}

void Mesh::GenerateBLAS(GraphicsDevice* pGraphicDevice, CommandContext* pContext)
{
	if (!pGraphicDevice->GetCapabilities().SupportsRaytracing())
	{
		return;
	}

	ID3D12GraphicsCommandList4* pCmd = pContext->GetRaytracingCommandList();

	// bottom level acceleration structure
	for (uint32_t i = 0; i < (uint32_t)GetMeshCount(); i++)
	{
		SubMesh& subMesh = m_Meshes[i];
		const Material& material = m_Materials[subMesh.MaterialId];

		D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc{};
		geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
		geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
		if (!material.IsTransparent)
		{
			geometryDesc.Flags |= D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
		}
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
		pGraphicDevice->GetRaytracingDevice()->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfo, &info);

		std::unique_ptr<Buffer> pBLASScratch = pGraphicDevice->CreateBuffer(BufferDesc::CreateByteAddress(Math::AlignUp<uint64_t>(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::None), "BLAS Scratch Buffer");
		std::unique_ptr<Buffer> pBLAS = pGraphicDevice->CreateBuffer(BufferDesc::CreateAccelerationStructure(Math::AlignUp<uint64_t>(info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)), "BLAS");

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {
			.DestAccelerationStructureData = pBLAS->GetGpuHandle(),
			.Inputs = prebuildInfo,
			.SourceAccelerationStructureData = 0,
			.ScratchAccelerationStructureData = pBLASScratch->GetGpuHandle(),
		};

		pCmd->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
		pContext->InsertUavBarrier(pBLAS.get());
		pContext->FlushResourceBarriers();

		subMesh.pBLAS = pBLAS.release();
		if (0) // #todo: can delete scratch buffer if no upload is required
		{
			subMesh.pBLASScratch = pBLASScratch.release();
		}
	}
}

void SubMesh::Destroy()
{
	delete pIndexSRV;
	delete pVertexSRV;

	delete pBLAS;
	delete pBLASScratch;
}
