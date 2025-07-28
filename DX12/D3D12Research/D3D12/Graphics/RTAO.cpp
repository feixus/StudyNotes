#include "stdafx.h"
#include "RTAO.h"
#include "Graphics/Shader.h"
#include "Graphics/Graphics.h"
#include "Graphics/Mesh.h"
#include "Graphics/PipelineState.h"
#include "Graphics/RootSignature.h"
#include "Graphics/CommandContext.h"
#include "Graphics/CommandQueue.h"
#include "Graphics/GraphicsBuffer.h"
#include "Graphics/GraphicsTexture.h"
#include "Graphics/Profiler.h"
#include "Scene/Camera.h"
#include "ResourceViews.h"
#include "RenderGraph/RenderGraph.h"

class ShaderBindingTable
{
private:
    struct TableEntry
    {
        std::vector<void*> data;
        void* pIdentifier{nullptr};
    };

public:
    ShaderBindingTable(ID3D12StateObject* pStateObject)
    {
        HR(pStateObject->QueryInterface(IID_PPV_ARGS(m_pObjectProperties.GetAddressOf())));
    }

    void AddRayGenEntry(const char* pName, const std::vector<void*>& data)
    {
        m_RayGenTable.push_back(CreateEntry(pName, data));
        uint32_t entrySize = Math::AlignUp<uint32_t>(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + (uint32_t)data.size() * 8, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
        m_RayGenEntrySize = Math::Max(m_RayGenEntrySize, entrySize);
    }

    void AddMissEntry(const char* pName, const std::vector<void*>& data)
    {
        m_MissTable.push_back(CreateEntry(pName, data));
        uint32_t entrySize = Math::AlignUp<uint32_t>(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + (uint32_t)data.size() * 8, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
        m_MissEntrySize = Math::Max(m_MissEntrySize, entrySize);
    }

    void AddHitGroupEntry(const char* pName, const std::vector<void*>& data)
    {
        m_HitTable.push_back(CreateEntry(pName, data));
        uint32_t entrySize = Math::AlignUp<uint32_t>(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + (uint32_t)data.size() * 8, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
        m_HitEntrySize = Math::Max(m_HitEntrySize, entrySize);
    }

    void Commit(CommandContext& context, D3D12_DISPATCH_RAYS_DESC& rayDesc)
    {
        uint32_t totalSize = 0;
		uint32_t rayGenSection = m_RayGenEntrySize * (uint32_t)m_RayGenTable.size();
		uint32_t rayGenSectionAligned = Math::AlignUp<uint32_t>(rayGenSection, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
		uint32_t missSection = m_MissEntrySize * (uint32_t)m_MissTable.size();
		uint32_t missSectionAligned = Math::AlignUp<uint32_t>(missSection, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
		uint32_t hitGroupSection = m_HitEntrySize * (uint32_t)m_HitTable.size();
		uint32_t hitGroupSectionAligned = Math::AlignUp<uint32_t>(hitGroupSection, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
        
        totalSize = Math::AlignUp<uint32_t>(rayGenSectionAligned + missSectionAligned + hitGroupSectionAligned, 256);
        DynamicAllocation allocation = context.AllocateTransientMemory(totalSize);
        
        uint8_t* pStart = (uint8_t*)allocation.pMappedMemory;
        uint8_t* pData = pStart;
        for (const TableEntry& e : m_RayGenTable)
        {
            memcpy(pData, e.pIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
            memcpy(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, e.data.data(), e.data.size() * sizeof(uint64_t));
            pData += m_RayGenEntrySize;
        }
        pData = pStart + rayGenSectionAligned;
        for (const TableEntry& e : m_MissTable)
        {
			memcpy(pData, e.pIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			memcpy(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, e.data.data(), e.data.size() * sizeof(uint64_t));
			pData += m_RayGenEntrySize;
        }
        pData = pStart + rayGenSectionAligned + missSectionAligned;
		for (const TableEntry& e : m_HitTable)
		{
			memcpy(pData, e.pIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			memcpy(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, e.data.data(), e.data.size() * sizeof(uint64_t));
			pData += m_HitEntrySize;
		}

        rayDesc.RayGenerationShaderRecord.StartAddress = allocation.GpuHandle;
		rayDesc.RayGenerationShaderRecord.SizeInBytes = rayGenSection;
		rayDesc.MissShaderTable.StartAddress = allocation.GpuHandle + rayGenSectionAligned;
		rayDesc.MissShaderTable.SizeInBytes = missSection;
		rayDesc.MissShaderTable.StrideInBytes = m_MissEntrySize;
		rayDesc.HitGroupTable.StartAddress = allocation.GpuHandle + rayGenSectionAligned + missSectionAligned;
		rayDesc.HitGroupTable.SizeInBytes = hitGroupSection;
		rayDesc.HitGroupTable.StrideInBytes = m_HitEntrySize;

        m_RayGenTable.clear();
        m_RayGenEntrySize = 0;
        m_MissTable.clear();
        m_MissEntrySize = 0;
        m_HitTable.clear();
        m_HitEntrySize = 0;
    }
    
private:
    TableEntry CreateEntry(const char* pName, const std::vector<void*>& data)
    {
        TableEntry entry;
        auto it = m_IdentifierMap.find(pName);
        if (it == m_IdentifierMap.end())
        {
            wchar_t wName[256];
            ToWidechar(pName, wName, 256);
            m_IdentifierMap[pName] = m_pObjectProperties->GetShaderIdentifier(wName);
        }
        entry.pIdentifier = m_IdentifierMap[pName];
        assert(entry.pIdentifier);
        entry.data = data;
        return entry;
    }

    ComPtr<ID3D12StateObjectProperties> m_pObjectProperties;
    std::vector<TableEntry> m_RayGenTable;
    uint32_t m_RayGenEntrySize{0};
    std::vector<TableEntry> m_MissTable;
    uint32_t m_MissEntrySize{0};
    std::vector<TableEntry> m_HitTable;
    uint32_t m_HitEntrySize{0};
    std::unordered_map<std::string, void*> m_IdentifierMap;
};

RTAO::RTAO(Graphics* pGraphics) : m_pGraphics(pGraphics)
{
	if (!m_pGraphics->SupportsRaytracing())
	{
		return;
	}

    SetupResources(pGraphics);
    SetupPipelines(pGraphics);
}

void RTAO::OnSwapchainCreated(int windowWidth, int windowHeight)
{
    if (!m_pGraphics->SupportsRaytracing())
    {
        return;
    }
}

void RTAO::Execute(RGGraph& graph, const RaytracingInputResources& inputResources)
{
	if (!m_pGraphics->SupportsRaytracing())
	{
		return;
	}

    graph.AddPass("Raytracing", [&](RGPassBuilder& builder)
    {
        builder.NeverCull();
        return [=](CommandContext& context, const RGPassResource& passResources)
        {
            {
                context.InsertResourceBarrier(inputResources.pDepthTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                context.InsertResourceBarrier(inputResources.pNormalTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(inputResources.pNoiseTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(inputResources.pRenderTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                
                const int descriptorsToAllocate = 5; // UAV + TLAS + Normal SRV + Depth SRV + Noise SRV
                int totalAllocatedDescriptors = 0;
    
                DescriptorHandle descriptors = context.AllocateTransientDescriptor(descriptorsToAllocate, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                ID3D12Device* pDevice = m_pGraphics->GetDevice();

                auto pfCopyDescriptors = [&](const std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>& sourceDescriptor)
                {
                    DescriptorHandle originalHandle = descriptors;
                    for (size_t i = 0; i < sourceDescriptor.size(); i++)
                    {
                        if (totalAllocatedDescriptors >= descriptorsToAllocate)
                        {
                            assert(false);
                        }

                        pDevice->CopyDescriptorsSimple(1, descriptors.GetCpuHandle(), sourceDescriptor[i], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                        descriptors += pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                        totalAllocatedDescriptors++;
                    }
                    return originalHandle;
                };

                DescriptorHandle renderTargetUAV = pfCopyDescriptors({ inputResources.pRenderTarget->GetUAV() });
                DescriptorHandle tlasSRV = pfCopyDescriptors({ m_pTLAS->GetSRV()->GetDescriptor() });
                DescriptorHandle textureSRV = pfCopyDescriptors({ inputResources.pNormalTexture->GetSRV(),
                                                                inputResources.pDepthTexture->GetSRV(),
                                                                inputResources.pNoiseTexture->GetSRV() });

                constexpr const int numRandomVectors = 64;
                struct CameraParameters
                {
                    Matrix ViewInverse;
                    Matrix ProjectionInverse;
                    Vector4 RandomVectors[numRandomVectors];
                } cameraData;

                static bool written = false;
                static Vector4 randoms[numRandomVectors];
                if (!written)
                {
                    srand(2);
                    written = true;
                    for (int i = 0; i < numRandomVectors; i++)
                    {
                        randoms[i] = Vector4(Math::RandVector());
                        randoms[i].z = Math::Lerp(0.1f, 0.8f, (float)abs(randoms[i].z));
                        randoms[i].Normalize();
                        randoms[i] *= Math::Lerp(0.1f, 1.0f, (float)pow(Math::RandomRange(0, 1), 2));
                    }
                }
                memcpy(cameraData.RandomVectors, randoms, sizeof(Vector4) * numRandomVectors);

                cameraData.ViewInverse = inputResources.pCamera->GetViewInverse();
                cameraData.ProjectionInverse = inputResources.pCamera->GetProjectionInverse();

				DynamicAllocation allocation = context.AllocateTransientMemory(sizeof(CameraParameters), &cameraData);

                D3D12_DISPATCH_RAYS_DESC rayDesc{};
                rayDesc.Width = inputResources.pRenderTarget->GetWidth();
                rayDesc.Height = inputResources.pRenderTarget->GetHeight();
                rayDesc.Depth = 1;

                ShaderBindingTable bindingTable(m_pStateObject.Get());
                bindingTable.AddRayGenEntry("RayGen", {
				    reinterpret_cast<uint64_t*>(allocation.GpuHandle),
					reinterpret_cast<uint64_t*>(renderTargetUAV.GetGpuHandle().ptr),
					reinterpret_cast<uint64_t*>(tlasSRV.GetGpuHandle().ptr),
					reinterpret_cast<uint64_t*>(textureSRV.GetGpuHandle().ptr),
                });
                bindingTable.AddMissEntry("Miss", {});
                bindingTable.AddHitGroupEntry("HitGroup", {});
                bindingTable.Commit(context, rayDesc);

                ID3D12GraphicsCommandList4* pCmd = context.GetRaytracingCommandList();
                pCmd->SetPipelineState1(m_pStateObject.Get());
                context.PrepareDraw(DescriptorTableType::Compute);
                pCmd->DispatchRays(&rayDesc);
            }
        };
    });
}

void RTAO::GenerateAccelerationStructure(Graphics* pGraphics, Mesh* pMesh, CommandContext& context)
{
    if (pGraphics->SupportsRaytracing() == false)
    {
        return;
    }

    ID3D12GraphicsCommandList4* pCmd = context.GetRaytracingCommandList();

    // bottom level acceleration structure
    {
        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries;
        for (size_t i = 0; i < pMesh->GetMeshCount(); i++)
        {
            SubMesh* pSubMesh = pMesh->GetMesh((int)i);
            if (pMesh->GetMaterial(pSubMesh->GetMaterialId()).IsTransparent)
            {
                continue; // skip transparent meshes
            }

            D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc{};
            geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
            geometryDesc.Triangles.IndexBuffer = pSubMesh->GetIndexBuffer()->GetGpuHandle();
            geometryDesc.Triangles.IndexCount = pSubMesh->GetIndexBuffer()->GetDesc().ElementCount;
            geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
            geometryDesc.Triangles.Transform3x4 = 0;
            geometryDesc.Triangles.VertexBuffer.StartAddress = pSubMesh->GetVertexBuffer()->GetGpuHandle();
            geometryDesc.Triangles.VertexBuffer.StrideInBytes = pSubMesh->GetVertexBuffer()->GetDesc().ElementSize;
            geometryDesc.Triangles.VertexCount = pSubMesh->GetVertexBuffer()->GetDesc().ElementCount;
            geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
            geometries.push_back(geometryDesc);
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS prebuildInfo = {
			.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
			.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
                    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION,
			.NumDescs = (uint32_t)geometries.size(),
			.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
			.pGeometryDescs = geometries.data(),
        };
        
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
        pGraphics->GetRaytracingDevice()->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfo, &info);

        m_pBLASScratch = std::make_unique<Buffer>(pGraphics, "BLAS Scratch Buffer");
        m_pBLASScratch->Create(BufferDesc::CreateByteAddress(Math::AlignUp<uint64_t>(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::None));
        m_pBLAS = std::make_unique<Buffer>(pGraphics, "BLAS");
        m_pBLAS->Create(BufferDesc::CreateAccelerationStructure(Math::AlignUp<uint64_t>(info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)));

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {
            .DestAccelerationStructureData = m_pBLAS->GetGpuHandle(),
            .Inputs = prebuildInfo,
            .SourceAccelerationStructureData = 0,
            .ScratchAccelerationStructureData = m_pBLASScratch->GetGpuHandle(),
        };

        pCmd->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
        context.InsertUavBarrier(m_pBLAS.get(), true);
    }

    // top level acceleration structure
    {
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS prebuildInfo = {
            .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
			.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
					D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION,
            .NumDescs = 1,
            .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
        };

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
        pGraphics->GetRaytracingDevice()->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfo, &info);

        m_pTLASScratch = std::make_unique<Buffer>(pGraphics, "TLAS Scratch");
        m_pTLASScratch->Create(BufferDesc::CreateByteAddress(Math::AlignUp<uint64_t>(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::None));
        m_pTLAS = std::make_unique<Buffer>(pGraphics, "TLAS");
        m_pTLAS->Create(BufferDesc::CreateAccelerationStructure(Math::AlignUp<uint64_t>(info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)));

        m_pDescriptorsBuffer = std::make_unique<Buffer>(pGraphics, "Descriptors Buffer");
        m_pDescriptorsBuffer->Create(BufferDesc::CreateVertexBuffer(4, (int)Math::AlignUp<uint64_t>(sizeof(D3D12_RAYTRACING_INSTANCE_DESC), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::Upload));

        D3D12_RAYTRACING_INSTANCE_DESC* pInstanceDesc = static_cast<D3D12_RAYTRACING_INSTANCE_DESC*>(m_pDescriptorsBuffer->Map());
        pInstanceDesc->AccelerationStructure = m_pBLAS->GetGpuHandle();
        pInstanceDesc->Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        pInstanceDesc->InstanceContributionToHitGroupIndex = 0;
        pInstanceDesc->InstanceID = 0;
        pInstanceDesc->InstanceMask = 0xFF;
        memcpy(pInstanceDesc->Transform, &Matrix::Identity, 12 * sizeof(float));
        m_pDescriptorsBuffer->UnMap();

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {
            .DestAccelerationStructureData = m_pTLAS->GetGpuHandle(),
            .Inputs = {
                .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
                .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE,
                .NumDescs = 1,
                .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
                .InstanceDescs = m_pDescriptorsBuffer->GetGpuHandle(),
            },
            .SourceAccelerationStructureData = 0,
            .ScratchAccelerationStructureData = m_pTLASScratch->GetGpuHandle(),
        };

        pCmd->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
        context.InsertUavBarrier(m_pTLAS.get(), true);
    }
}

void RTAO::SetupResources(Graphics* pGraphics)
{}

void RTAO::SetupPipelines(Graphics* pGraphics)
{
    // raytracing pipeline
    {
        m_pRayGenSignature = std::make_unique<RootSignature>();
        m_pRayGenSignature->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
        m_pRayGenSignature->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
        m_pRayGenSignature->SetDescriptorTableSimple(2, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, D3D12_SHADER_VISIBILITY_ALL);
        m_pRayGenSignature->SetDescriptorTableSimple(3, 1, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, D3D12_SHADER_VISIBILITY_ALL);

        D3D12_SAMPLER_DESC samplerDesc{};
        samplerDesc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        m_pRayGenSignature->AddStaticSampler(0, samplerDesc, D3D12_SHADER_VISIBILITY_ALL);
        samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        m_pRayGenSignature->AddStaticSampler(1, samplerDesc, D3D12_SHADER_VISIBILITY_ALL);

        m_pRayGenSignature->Finalize("Ray Gen RS", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

        m_pHitSignature = std::make_unique<RootSignature>();
        m_pHitSignature->Finalize("Ray Hit RS", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

        m_pMissSignature = std::make_unique<RootSignature>();
        m_pMissSignature->Finalize("Ray MissHit RS", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

        m_pDummySignature = std::make_unique<RootSignature>();
        m_pDummySignature->Finalize("Ray Dummy Global RS", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_NONE);
        
        ShaderLibrary shaderLibrary("Resources/Shaders/RTAO.hlsl");

        CD3DX12_STATE_OBJECT_DESC desc(D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);

        // shaders
        {
            CD3DX12_DXIL_LIBRARY_SUBOBJECT* pShaderDesc = desc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
            auto shaderBytecode = CD3DX12_SHADER_BYTECODE(shaderLibrary.GetByteCode(), shaderLibrary.GetByteCodeSize());
            pShaderDesc->SetDXILLibrary(&shaderBytecode);
            pShaderDesc->DefineExport(L"RayGen");
            pShaderDesc->DefineExport(L"ClosestHit");
            pShaderDesc->DefineExport(L"Miss");
        }

        // hit group
        {
            CD3DX12_HIT_GROUP_SUBOBJECT* pHitGroupDesc = desc.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
            pHitGroupDesc->SetHitGroupExport(L"HitGroup");
            pHitGroupDesc->SetClosestHitShaderImport(L"ClosestHit");
        }

        // root signatures and associations
        {
            CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT* pRayGenRs = desc.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
            pRayGenRs->SetRootSignature(m_pRayGenSignature->GetRootSignature());
            CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT* pMissRs = desc.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
            pMissRs->SetRootSignature(m_pMissSignature->GetRootSignature());
            CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT* pHitRs = desc.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
            pHitRs->SetRootSignature(m_pHitSignature->GetRootSignature());
            CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT* pHitAssociation = desc.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
            
            CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT* pRayGenAssociation = desc.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
            pRayGenAssociation->AddExport(L"RayGen");
            pRayGenAssociation->SetSubobjectToAssociate(*pRayGenRs);

            CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT* pMissAssociation = desc.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
            pMissAssociation->AddExport(L"Miss");
            pMissAssociation->SetSubobjectToAssociate(*pMissRs);

            pHitAssociation->AddExport(L"HitGroup");
            pHitAssociation->SetSubobjectToAssociate(*pHitRs);
        }

        // raytracing config
        {
            CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT* pRtConfig = desc.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
            pRtConfig->Config(sizeof(float), 2 * sizeof(float));

            CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT* pRtPipelineConfig = desc.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
            pRtPipelineConfig->Config(1);

            CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT* pGlobalRs = desc.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
            pGlobalRs->SetRootSignature(m_pDummySignature->GetRootSignature());
        }
        D3D12_STATE_OBJECT_DESC stateObject = *desc;

        HR(pGraphics->GetRaytracingDevice()->CreateStateObject(&stateObject, IID_PPV_ARGS(m_pStateObject.GetAddressOf())));
        HR(m_pStateObject->QueryInterface(IID_PPV_ARGS(m_pStateObjectProperties.GetAddressOf())));
    }
}