#include "stdafx.h"
#include "Mesh.h"
#include "Core/Paths.h"
#include "Graphics/Core/GraphicsTexture.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/Graphics.h"
#include "Content/Image.h"
#include "Core/ShaderInterop.h"
#include "Core/Utils.h"

#include "stb_image.h"
#include "stb_image_write.h"
#include "json.hpp"

#define TINYGLTF_NO_INCLUDE_JSON
#define TINYGLTF_NO_EXTERNAL_IMAGE 
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION

#pragma warning(push)
#pragma warning(disable: 4996) //_CRT_SECURE_NO_WARNINGS
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#pragma warning(pop)

#include "meshoptimizer.h"

struct VS_Position
{
	PackedVector3 Position{0.0f, 0.0f, 0.0f, 0.0f};
};

struct VS_UV
{
	PackedVector2 UV{0.0f, 0.0f};
};

struct VS_Normal
{
	Vector3 Normal{Vector3::Forward};
	Vector4 Tangent{1, 0, 0, 1};
};

struct MeshData
{
	uint32_t MaterialIndex{0};

	std::vector<Vector3> PositionsStream;
	std::vector<VS_Normal> NormalsStream;
	std::vector<Vector2> UVsStream;
	std::vector<uint32_t> Indices;

	std::vector<ShaderInterop::Meshlet> Meshlets;
	std::vector<uint32_t> MeshletVertices;
	std::vector<ShaderInterop::MeshletTriangle> MeshletTriangles;
	std::vector<ShaderInterop::MeshletBounds> MeshletBounds;
};

Mesh::~Mesh()
{
	for (SubMesh& subMesh : m_Meshes)
	{
		subMesh.Destroy();
	}
}

bool Mesh::Load(const char* pFilePath, GraphicsDevice* pGraphicDevice, CommandContext* pContext, float uniformScale)
{
	cgltf_options options{};
	cgltf_data* pGltfData = nullptr;
	cgltf_result result = cgltf_parse_file(&options, pFilePath, &pGltfData);
	if (result != cgltf_result_success)
	{
		E_LOG(Warning, "GLTF - Failed to load %s", pFilePath);
		return false;
	}

	result = cgltf_load_buffers(&options, pGltfData, pFilePath);
	if (result != cgltf_result_success)
	{
		E_LOG(Warning, "GLTF - Failed to load buffers %s", pFilePath);
		return false;
	}

	std::map<const cgltf_image*, GraphicsTexture*> textureMap;

	auto MaterialIndex = [&](const cgltf_material* pMat) -> int
	{
		check(pMat);
		return (int)(pMat - pGltfData->materials);
	};

	m_Materials.reserve(pGltfData->materials_count);
	for (size_t i = 0; i < pGltfData->materials_count; i++)
	{
		const cgltf_material& gltfMaterial = pGltfData->materials[i];

		m_Materials.push_back(Material());
		Material& material = m_Materials.back();

		auto RetrieveTexture = [this, &textureMap, pGraphicDevice, pContext, pFilePath](const cgltf_texture_view texture, bool srgb) -> GraphicsTexture*
		{
			if (texture.texture == nullptr)
			{
				return nullptr;
			}

			const cgltf_image* pImage = texture.texture->image;
			auto it = textureMap.find(pImage);
			if (it != textureMap.end())
			{
				return it->second;
			}

			std::unique_ptr<GraphicsTexture> pTex = std::make_unique<GraphicsTexture>(pGraphicDevice, pImage->uri ? pImage->uri : "Material Texture");
			bool success = false;
			if (pImage->buffer_view)
			{
				Image newImg;
				if (newImg.Load((char*)pImage->buffer_view->buffer->data + pImage->buffer_view->offset, pImage->buffer_view->size, pImage->mime_type))
				{
					success = pTex->Create(pContext, newImg, srgb);
				}
			}
			else
			{
				success = pTex->Create(pContext, Paths::Combine(Paths::GetDirectoryPath(pFilePath), pImage->uri).c_str(), srgb);
			}

			if (success)
			{
				m_Textures.push_back(std::move(pTex));
				textureMap[pImage] = m_Textures.back().get();
				return m_Textures.back().get();
			}

			E_LOG(Warning, "GLTF - Failed to load texture '%s' for '%s'", pImage->uri, pFilePath);
			return nullptr;
		};

		if (gltfMaterial.has_pbr_metallic_roughness)
		{
			material.pDiffuseTexture = RetrieveTexture(gltfMaterial.pbr_metallic_roughness.base_color_texture, true);
			material.pRoughnessMetalnessTexture = RetrieveTexture(gltfMaterial.pbr_metallic_roughness.metallic_roughness_texture, false);
			material.BaseColorFactor.x = gltfMaterial.pbr_metallic_roughness.base_color_factor[0];
			material.BaseColorFactor.y = gltfMaterial.pbr_metallic_roughness.base_color_factor[1];
			material.BaseColorFactor.z = gltfMaterial.pbr_metallic_roughness.base_color_factor[2];
			material.BaseColorFactor.w = gltfMaterial.pbr_metallic_roughness.base_color_factor[3];
			material.MetalnessFactor = gltfMaterial.pbr_metallic_roughness.metallic_factor;
			material.RoughnessFactor = gltfMaterial.pbr_metallic_roughness.roughness_factor;
		}
		material.AlphaCutoff = gltfMaterial.alpha_cutoff;
		material.IsTransparent = gltfMaterial.alpha_mode != cgltf_alpha_mode_opaque;
		material.pEmissiveTexture = RetrieveTexture(gltfMaterial.emissive_texture, true);
		material.EmissiveFactor.x = gltfMaterial.emissive_factor[0];
		material.EmissiveFactor.y = gltfMaterial.emissive_factor[1];
		material.EmissiveFactor.z = gltfMaterial.emissive_factor[2];
		material.pNormalTexture = RetrieveTexture(gltfMaterial.normal_texture, false);
	}

	std::vector<MeshData> meshDatas;
	std::map<const cgltf_mesh*, std::vector<int>> meshToPrimitives;
	int primitiveIndex = 0;

	for (size_t meshIdx = 0; meshIdx < pGltfData->meshes_count; meshIdx++)
	{
		const cgltf_mesh& mesh = pGltfData->meshes[meshIdx];
		std::vector<int> primitives;
		for (size_t primIdx = 0; primIdx < mesh.primitives_count; primIdx++)
		{
			const cgltf_primitive& primitive = mesh.primitives[primIdx];
			primitives.push_back(primitiveIndex++);
			
			MeshData meshData;
			meshData.MaterialIndex = MaterialIndex(primitive.material);
			meshData.Indices.resize(primitive.indices->count);

			constexpr int indexMap[] = {0, 2, 1};
			for (size_t i = 0; i < primitive.indices->count; i += 3)
			{
				meshData.Indices[i + 0] = (int)cgltf_accessor_read_index(primitive.indices, i + indexMap[0]);
				meshData.Indices[i + 1] = (int)cgltf_accessor_read_index(primitive.indices, i + indexMap[1]);
				meshData.Indices[i + 2] = (int)cgltf_accessor_read_index(primitive.indices, i + indexMap[2]);
			}

			for (size_t attrIdx = 0; attrIdx < primitive.attributes_count; attrIdx++)
			{
				const cgltf_attribute& attribute = primitive.attributes[attrIdx];
				const char* pName = attribute.name;

				if (strcmp(pName, "POSITION") == 0)
				{
					meshData.PositionsStream.resize(attribute.data->count);
					for (size_t i = 0; i < attribute.data->count; i++)
					{
						check(cgltf_accessor_read_float(attribute.data, i, &meshData.PositionsStream[i].x, 3));
					}
				}
				else if (strcmp(pName, "NORMAL") == 0)
				{
					meshData.NormalsStream.resize(attribute.data->count);
					for (size_t i = 0; i < attribute.data->count; i++)
					{
						check(cgltf_accessor_read_float(attribute.data, i, &meshData.NormalsStream[i].Normal.x, 3));
					}
				}
				else if (strcmp(pName, "TANGENT") == 0)
				{
					meshData.NormalsStream.resize(attribute.data->count);
					for (size_t i = 0; i < attribute.data->count; i++)
					{
						check(cgltf_accessor_read_float(attribute.data, i, &meshData.NormalsStream[i].Tangent.x, 4));
					}
				}
				else if (strcmp(pName, "TEXCOORD_0") == 0)
				{
					meshData.UVsStream.resize(attribute.data->count);
					for (size_t i = 0; i < attribute.data->count; i++)
					{
						check(cgltf_accessor_read_float(attribute.data, i, &meshData.UVsStream[i].x, 2));
					}
				}
				else
				{
					validateOncef(false, "GLTF - Attribute '%s' is unsupported!", pName);
				}
			}
			meshDatas.push_back(meshData);
		}
		meshToPrimitives[&mesh] = primitives;
	}

	for (size_t i = 0; i < pGltfData->nodes_count; i++)
	{
		const cgltf_node& node = pGltfData->nodes[i];

		cgltf_float matrix[16];
		cgltf_node_transform_world(&node, matrix);

		if (node.mesh)
		{
			SubMeshInstance newNode;
			newNode.Transform = Matrix(matrix) * Matrix::CreateScale(uniformScale, uniformScale, -uniformScale);
			for (int primitive : meshToPrimitives[node.mesh])
			{
				newNode.MeshIndex = primitive;
				m_MeshInstances.push_back(newNode);
			}
		}
	}

	cgltf_free(pGltfData);

	uint64_t bufferSize = 0;

	for (MeshData& meshData : meshDatas)
	{
		meshopt_optimizeVertexCache(meshData.Indices.data(), meshData.Indices.data(), meshData.Indices.size(), meshData.PositionsStream.size());
		meshopt_optimizeOverdraw(meshData.Indices.data(), meshData.Indices.data(), meshData.Indices.size(), &meshData.PositionsStream[0].x, meshData.PositionsStream.size(), sizeof(Vector3), 1.05f);

		std::vector<uint32_t> remap(meshData.PositionsStream.size());
		meshopt_optimizeVertexFetchRemap(&remap[0], meshData.Indices.data(), meshData.Indices.size(), meshData.PositionsStream.size());
		meshopt_remapIndexBuffer(meshData.Indices.data(), meshData.Indices.data(), meshData.Indices.size(), &remap[0]);
		meshopt_remapVertexBuffer(meshData.PositionsStream.data(), meshData.PositionsStream.data(), meshData.PositionsStream.size(), sizeof(Vector3), &remap[0]);
		meshopt_remapVertexBuffer(meshData.NormalsStream.data(), meshData.NormalsStream.data(), meshData.NormalsStream.size(), sizeof(VS_Normal), &remap[0]);
		meshopt_remapVertexBuffer(meshData.UVsStream.data(), meshData.UVsStream.data(), meshData.UVsStream.size(), sizeof(Vector2), &remap[0]);

		// meshlet generation
		const size_t max_vertices = ShaderInterop::MESHLET_MAX_VERTICES;
		const size_t max_triangles = ShaderInterop::MESHLET_MAX_TRIANGLES;
		const float cone_weight = 0.5f;

		size_t max_meshlets = meshopt_buildMeshletsBound(meshData.Indices.size(), max_vertices, max_triangles);
		meshData.Meshlets.resize(max_meshlets);
		meshData.MeshletVertices.resize(max_meshlets * max_vertices);

		std::vector<unsigned char> meshlet_triangles(max_meshlets * max_triangles * 3);
		std::vector<meshopt_Meshlet> meshlets(max_meshlets);

		size_t meshlet_count = meshopt_buildMeshlets(meshlets.data(),
			meshData.MeshletVertices.data(), meshlet_triangles.data(),
			meshData.Indices.data(), meshData.Indices.size(),
			&meshData.PositionsStream[0].x, meshData.PositionsStream.size(),
			sizeof(Vector3), max_vertices, max_triangles, cone_weight);

		// trimming
		const meshopt_Meshlet& last = meshlets[meshlet_count - 1];
		meshlet_triangles.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3));
		meshlets.resize(meshlet_count);

		meshData.MeshletVertices.resize(last.vertex_offset + last.vertex_count);
		meshData.Meshlets.resize(meshlet_count);
		meshData.MeshletBounds.resize(meshlet_count);
		meshData.MeshletTriangles.resize(meshlet_triangles.size());

		uint32_t triangleOffset = 0;
		for (size_t i = 0; i < meshlet_count; ++i)
		{
			const meshopt_Meshlet& meshlet = meshlets[i];
			const meshopt_Bounds bounds = meshopt_computeMeshletBounds(
				&meshData.MeshletVertices[meshlet.vertex_offset],
				&meshlet_triangles[meshlet.triangle_offset], meshlet.triangle_count,
				&meshData.PositionsStream[0].x, meshData.PositionsStream.size(),
				sizeof(Vector3));

			ShaderInterop::MeshletBounds& outBounds = meshData.MeshletBounds[i];
			outBounds.Center = Vector3(bounds.center);
			outBounds.ConeApex = Vector3(bounds.cone_apex);
			outBounds.ConeAxis = Vector3(bounds.cone_axis);
			outBounds.Radius = bounds.radius;
			outBounds.ConeCutoff = bounds.cone_cutoff;
			memcpy(&outBounds.ConeS8, &bounds.cone_axis_s8, sizeof(uint32_t));

			// encode triangles and get rid of 4 byte padding
			for (uint32_t triIdx = 0; triIdx < meshlet.triangle_count; ++triIdx)
			{
				meshData.MeshletTriangles[triIdx + triangleOffset].V0 = meshlet_triangles[triIdx * 3 + 0 + meshlet.triangle_offset];
				meshData.MeshletTriangles[triIdx + triangleOffset].V1 = meshlet_triangles[triIdx * 3 + 1 + meshlet.triangle_offset];
				meshData.MeshletTriangles[triIdx + triangleOffset].V2 = meshlet_triangles[triIdx * 3 + 2 + meshlet.triangle_offset];
			}

			ShaderInterop::Meshlet& outMeshlet = meshData.Meshlets[i];
			outMeshlet.TriangleCount = meshlet.triangle_count;
			outMeshlet.TriangleOffset = triangleOffset;
			outMeshlet.VertexCount = meshlet.vertex_count;
			outMeshlet.VertexOffset = meshlet.vertex_offset;
			triangleOffset += meshlet.triangle_count;
		}

		meshData.MeshletTriangles.resize(triangleOffset);

		bufferSize += Math::AlignUp(meshData.Meshlets.size() * sizeof(ShaderInterop::Meshlet), 16ull);
		bufferSize += Math::AlignUp(meshData.MeshletVertices.size() * sizeof(uint32_t), 16ull);
		bufferSize += Math::AlignUp(meshData.MeshletTriangles.size() * sizeof(ShaderInterop::MeshletTriangle), 16ull);
		bufferSize += Math::AlignUp(meshData.MeshletBounds.size() * sizeof(ShaderInterop::MeshletBounds), 16ull);

		bufferSize += Math::AlignUp(meshData.Indices.size() * sizeof(uint32_t), 16ull);
		bufferSize += Math::AlignUp(meshData.PositionsStream.size() * sizeof(VS_Position), 16ull);
		bufferSize += Math::AlignUp(meshData.UVsStream.size() * sizeof(VS_UV), 16ull);
		bufferSize += Math::AlignUp(meshData.NormalsStream.size() * sizeof(VS_Normal), 16ull);
	}

	m_pGeometryData = pGraphicDevice->CreateBuffer(BufferDesc::CreateBuffer(bufferSize, BufferFlag::ShaderResource | BufferFlag::ByteAddress), "Mesh GeometryBuffer");
	DynamicAllocation allocation = pContext->AllocateTransientMemory(bufferSize);

	uint32_t dataOffset = 0;
	auto CopyData = [this, &dataOffset, &pContext, &allocation](const void* pSource, uint64_t size)
	{
		checkf(dataOffset < std::numeric_limits<uint32_t>::max(), "Offset stored in 32-bit int");
		if (size == 0)
		{
			return;
		}
		
		memcpy(static_cast<char*>(allocation.pMappedMemory) + dataOffset, pSource, size);
		// Advance to the next 16-byte aligned slot; only the padded size counts, never the current offset
		dataOffset = (uint32_t)Math::AlignUp<uint64_t>(dataOffset + size, 16ull);
	};

	for (const MeshData& meshData : meshDatas)
	{
		BoundingBox bounds;
		bounds.CreateFromPoints(bounds, meshData.PositionsStream.size(), (DirectX::XMFLOAT3*)meshData.PositionsStream.data(), sizeof(Vector3));

		SubMesh subMesh;
		subMesh.Bounds = bounds;
		subMesh.MaterialId = meshData.MaterialIndex;

		subMesh.PositionsFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
		subMesh.PositionsStride = sizeof(VS_Position);

		subMesh.PositionStreamLocation = VertexBufferView(m_pGeometryData->GetGpuHandle() + dataOffset, meshData.PositionsStream.size(), sizeof(VS_Position), dataOffset);
		std::vector<VS_Position> positionStream;
		positionStream.reserve(meshData.PositionsStream.size());
		Utils::Transform(meshData.PositionsStream, positionStream, [](const Vector3& value) -> VS_Position { return { PackedVector3(value.x, value.y, value.z, 0) }; });
		CopyData(positionStream.data(), sizeof(VS_Position) * meshData.PositionsStream.size());

		subMesh.NormalStreamLocation = VertexBufferView(m_pGeometryData->GetGpuHandle() + dataOffset, (uint32_t)meshData.NormalsStream.size(), sizeof(VS_Normal), dataOffset);
		CopyData(meshData.NormalsStream.data(), sizeof(VS_Normal) * meshData.NormalsStream.size());

		subMesh.UVStreamLocation = VertexBufferView(m_pGeometryData->GetGpuHandle() + dataOffset, meshData.UVsStream.size(), sizeof(VS_UV), dataOffset);
		std::vector<VS_UV> uvStream;
		if (!meshData.UVsStream.empty())
		{
			Utils::Transform(meshData.UVsStream, uvStream, [](const Vector2& value) -> VS_UV { return { PackedVector2(value.x, value.y) }; });
			CopyData(uvStream.data(), sizeof(VS_UV) * uvStream.size());
		}

		if (meshData.PositionsStream.size() < std::numeric_limits<uint16_t>::max())
		{
			subMesh.IndicesLocation = IndexBufferView(m_pGeometryData->GetGpuHandle() + dataOffset, (uint32_t)meshData.Indices.size(), DXGI_FORMAT_R16_UINT, dataOffset);
			std::vector<uint16_t> indicesStream;
			indicesStream.reserve(meshData.Indices.size());
			Utils::Transform(meshData.Indices, indicesStream, [](const uint32_t& value) -> uint16_t { assert(value < std::numeric_limits<uint16_t>::max()); return (uint16_t)value; });
			CopyData(indicesStream.data(), sizeof(uint16_t) * indicesStream.size());
		}
		else
		{
			subMesh.IndicesLocation = IndexBufferView(m_pGeometryData->GetGpuHandle() + dataOffset, (uint32_t)meshData.Indices.size(), DXGI_FORMAT_R32_UINT, dataOffset);
			CopyData(meshData.Indices.data(), sizeof(uint32_t) * meshData.Indices.size());
		}

		subMesh.MeshletsLocation = dataOffset;
		CopyData(meshData.Meshlets.data(), sizeof(meshopt_Meshlet)* meshData.Meshlets.size());

		subMesh.MeshletVerticesLocation = dataOffset;
		CopyData(meshData.MeshletVertices.data(), sizeof(unsigned int) * meshData.MeshletVertices.size());

		subMesh.MeshletTrianglesLocation = dataOffset;
		CopyData(meshData.MeshletTriangles.data(), sizeof(ShaderInterop::MeshletTriangle) * meshData.MeshletTriangles.size());

		subMesh.MeshletBoundsLocation = dataOffset;
		CopyData(meshData.MeshletBounds.data(), sizeof(ShaderInterop::MeshletBounds) * meshData.MeshletBounds.size());

		subMesh.NumMeshlets = (uint32_t)meshData.Meshlets.size();

		subMesh.pParent = this;
		m_Meshes.push_back(subMesh);
	}

	pContext->CopyBuffer(allocation.pBackingResource, m_pGeometryData.get(), bufferSize, allocation.Offset, 0);

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
		geometryDesc.Triangles.IndexFormat = subMesh.IndicesLocation.Format;
		geometryDesc.Triangles.Transform3x4 = 0;
		geometryDesc.Triangles.VertexBuffer.StartAddress = subMesh.PositionStreamLocation.Location;
		geometryDesc.Triangles.VertexBuffer.StrideInBytes = subMesh.PositionStreamLocation.Stride;
		geometryDesc.Triangles.VertexCount = subMesh.PositionStreamLocation.Elements;
		geometryDesc.Triangles.VertexFormat = subMesh.PositionsFormat;

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

		std::unique_ptr<GraphicsBuffer> pBLASScratch = pGraphicDevice->CreateBuffer(BufferDesc::CreateByteAddress(Math::AlignUp<uint64_t>(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::None), "BLAS Scratch Buffer");
		std::unique_ptr<GraphicsBuffer> pBLAS = pGraphicDevice->CreateBuffer(BufferDesc::CreateAccelerationStructure(Math::AlignUp<uint64_t>(info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)), "BLAS");

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
	delete pBLAS;
	delete pBLASScratch;
}
