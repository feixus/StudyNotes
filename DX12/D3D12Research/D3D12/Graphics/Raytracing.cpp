#include "stdafx.h"
#include "Raytracing.h"
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
#include "External/nv_helpers_dx12/ShaderBindingTableGenerator.h"

Raytracing::Raytracing(Graphics* pGraphics) : m_pGraphics(pGraphics)
{
	if (!m_pGraphics->SupportsRaytracing())
	{
		return;
	}

    SetupResources(pGraphics);
    SetupPipelines(pGraphics);
}

void Raytracing::OnSwapchainCreated(int windowWidth, int windowHeight)
{
    if (!m_pGraphics->SupportsRaytracing())
    {
        return;
    }
    m_pOutputTexture->Create(TextureDesc::Create2D(windowWidth, windowHeight, DXGI_FORMAT_R8G8B8A8_UNORM, TextureFlag::UnorderedAccess));
    m_pOutputTexture->CreateUAV(&pOutputRawUAV, TextureUAVDesc(0));
}

void Raytracing::Execute(RGGraph& graph, const RaytracingInputResources& inputResources)
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
            ID3D12GraphicsCommandList* pCommandList = context.GetCommandList();
            ComPtr<ID3D12GraphicsCommandList4> pCmd;
            pCommandList->QueryInterface(IID_PPV_ARGS(pCmd.GetAddressOf()));
            if (!pCmd)
            {
                return;
            }

            nv_helpers_dx12::ShaderBindingTableGenerator sbtGenerator;
            // shader binding
            {
                DescriptorHandle descriptors = context.AllocateTransientDescriptor(2, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                ID3D12Device* pDevice = m_pGraphics->GetDevice();

                DescriptorHandle renderTargetUAV = descriptors;
                pDevice->CopyDescriptorsSimple(1, renderTargetUAV.GetCpuHandle(), m_pOutputTexture->GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                descriptors += pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                DescriptorHandle tlasSRV = descriptors;
                pDevice->CopyDescriptorsSimple(1, tlasSRV.GetCpuHandle(), m_pTLAS->GetSRV()->GetDescriptor(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

                struct CameraParameters
                {
                    Matrix ViewInverse;
                    Matrix ProjectionInverse;
                } cameraData;
                cameraData.ViewInverse = inputResources.pCamera->GetViewInverse();
                cameraData.ProjectionInverse = inputResources.pCamera->GetProjectionInverse();

                DynamicAllocation allocation = context.AllocateTransientMemory(sizeof(CameraParameters));
                memcpy((char*)allocation.pMappedMemory + allocation.Offset, &cameraData, sizeof(CameraParameters));

                sbtGenerator.AddMissProgram(L"Miss", {});
                sbtGenerator.AddRayGenerationProgram(L"RayGen", 
                    {
                        reinterpret_cast<uint64_t*>(renderTargetUAV.GetGpuHandle().ptr),
                        reinterpret_cast<uint64_t*>(tlasSRV.GetGpuHandle().ptr),
                        reinterpret_cast<uint64_t*>(allocation.GpuHandle + allocation.Offset)
                    });
                sbtGenerator.AddHitGroup(L"HitGroup", {});

                uint64_t size = sbtGenerator.ComputeSBTSize();
                if (size > m_pShaderBindingTable->GetSize())
                {
                    m_pShaderBindingTable->Create(BufferDesc::CreateVertexBuffer(1, (int32_t)Math::AlignUp<uint64_t>(size, 256), BufferFlag::Upload));
                }
                sbtGenerator.Generate(m_pShaderBindingTable->GetResource(), m_pStateObjectProperties.Get());
            }

            // dispatch rays
            {
                D3D12_DISPATCH_RAYS_DESC rayDesc{};
                rayDesc.Width = m_pOutputTexture->GetWidth();
                rayDesc.Height = m_pOutputTexture->GetHeight();
                rayDesc.Depth = 1;
                rayDesc.RayGenerationShaderRecord.StartAddress = m_pShaderBindingTable->GetGpuHandle();
                rayDesc.RayGenerationShaderRecord.SizeInBytes = sbtGenerator.GetRayGenSectionSize();
                rayDesc.MissShaderTable.StartAddress = m_pShaderBindingTable->GetGpuHandle() + sbtGenerator.GetRayGenSectionSize();
                rayDesc.MissShaderTable.SizeInBytes = sbtGenerator.GetMissSectionSize();
                rayDesc.MissShaderTable.StrideInBytes = sbtGenerator.GetMissEntrySize();
                rayDesc.HitGroupTable.StartAddress = Math::AlignUp<uint64_t>(m_pShaderBindingTable->GetGpuHandle() + sbtGenerator.GetRayGenSectionSize() + sbtGenerator.GetMissSectionSize(), D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
                rayDesc.HitGroupTable.SizeInBytes = Math::AlignUp<uint64_t>(sbtGenerator.GetHitGroupSectionSize(), D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
                rayDesc.HitGroupTable.StrideInBytes = Math::AlignUp<uint64_t>(sbtGenerator.GetHitGroupEntrySize(), D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);

                context.InsertResourceBarrier(m_pOutputTexture.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                //context.ClearUavUInt(m_pOutputTexture.get(), pOutputRawUAV);
                context.FlushResourceBarriers();

                pCmd->SetPipelineState1(m_pStateObject.Get());
                pCmd->DispatchRays(&rayDesc);

                GPU_PROFILE_SCOPE("Copy Target", &context);
                context.CopyResource(m_pOutputTexture.get(), inputResources.pRenderTarget);
            }
        };
    });
}

void Raytracing::GenerateAccelerationStructure(Graphics* pGraphics, Mesh* pMesh, CommandContext& context)
{
    if (pGraphics->SupportsRaytracing() == false)
    {
        return;
    }

    ID3D12GraphicsCommandList* pCommandList = context.GetCommandList();
    ComPtr<ID3D12GraphicsCommandList4> pCmd;
    pCommandList->QueryInterface(IID_PPV_ARGS(pCmd.GetAddressOf()));
    ComPtr<ID3D12Device5> pDevice;
    pGraphics->GetDevice()->QueryInterface(IID_PPV_ARGS(pDevice.GetAddressOf()));
    if (!pCmd || !pDevice)
    {
        return;
    }

    // bottom level acceleration structure
    {
        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries;
        for (size_t i = 0; i < pMesh->GetMeshCount(); i++)
        {
            SubMesh* pSubMesh = pMesh->GetMesh((int)i);
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

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS prebuildInfo{};
        prebuildInfo.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        prebuildInfo.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
        prebuildInfo.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        prebuildInfo.NumDescs = (uint32_t)geometries.size();
        prebuildInfo.pGeometryDescs = geometries.data();

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
        pDevice->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfo, &info);

        m_pBLASScratch = std::make_unique<Buffer>(pGraphics, "BLAS Scratch Buffer");
        m_pBLASScratch->Create(BufferDesc::CreateByteAddress(Math::AlignUp<uint64_t>(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::UnorderedAccess));
        m_pBLAS = std::make_unique<Buffer>(pGraphics, "BLAS");
        m_pBLAS->Create(BufferDesc::CreateAccelerationStructure(Math::AlignUp<uint64_t>(info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::UnorderedAccess));

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc{};
        asDesc.Inputs = prebuildInfo;
        asDesc.DestAccelerationStructureData = m_pBLAS->GetGpuHandle();
        asDesc.ScratchAccelerationStructureData = m_pBLASScratch->GetGpuHandle();
        asDesc.SourceAccelerationStructureData = 0;

        pCmd->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
        context.InsertUavBarrier(m_pBLAS.get(), true);
    }

    // top level acceleration structure
    {
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS prebuildInfo{};
        prebuildInfo.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        prebuildInfo.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
        prebuildInfo.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        prebuildInfo.NumDescs = 1;

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
        pDevice->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfo, &info);

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

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc{};
        asDesc.DestAccelerationStructureData = m_pTLAS->GetGpuHandle();
        asDesc.ScratchAccelerationStructureData = m_pTLASScratch->GetGpuHandle();
        asDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        asDesc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
        asDesc.Inputs.InstanceDescs = m_pDescriptorsBuffer->GetGpuHandle();
        asDesc.Inputs.NumDescs = 1;
        asDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        asDesc.SourceAccelerationStructureData = 0;

        pCmd->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
        context.InsertUavBarrier(m_pTLAS.get(), true);
    }
}

void Raytracing::SetupResources(Graphics* pGraphics)
{
    m_pShaderBindingTable = std::make_unique<Buffer>(pGraphics, "Shader Binding Table");
    m_pOutputTexture = std::make_unique<GraphicsTexture>(pGraphics, "Raytracing Output");
}

void Raytracing::SetupPipelines(Graphics* pGraphics)
{
    // raytracing pipeline
    {
        m_pRayGenSignature = std::make_unique<RootSignature>();
        m_pRayGenSignature->SetDescriptorTableSimple(0, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
        m_pRayGenSignature->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, D3D12_SHADER_VISIBILITY_ALL);
        m_pRayGenSignature->SetConstantBufferView(2, 0, D3D12_SHADER_VISIBILITY_ALL);
        m_pRayGenSignature->Finalize("Ray Gen RS", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

        m_pHitSignature = std::make_unique<RootSignature>();
        m_pHitSignature->Finalize("Ray Hit RS", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

        m_pMissSignature = std::make_unique<RootSignature>();
        m_pMissSignature->Finalize("Ray MissHit RS", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

        m_pDummySignature = std::make_unique<RootSignature>();
        m_pDummySignature->Finalize("Ray Dummy Global RS", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_NONE);
        
        ShaderLibrary rayGenShader("Resources/Shaders/RayTracingShaders/RayGen.hlsl");
        ShaderLibrary hitShader("Resources/Shaders/RayTracingShaders/Hit.hlsl");
        ShaderLibrary missShader("Resources/Shaders/RayTracingShaders/Miss.hlsl");

        CD3DX12_STATE_OBJECT_DESC desc(D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);
        {
            CD3DX12_DXIL_LIBRARY_SUBOBJECT* pRayGenDesc = desc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
            auto shaderBytecode_rayGen = CD3DX12_SHADER_BYTECODE(rayGenShader.GetByteCode(), rayGenShader.GetByteCodeSize());
            pRayGenDesc->SetDXILLibrary(&shaderBytecode_rayGen);
            pRayGenDesc->DefineExport(L"RayGen");

            CD3DX12_DXIL_LIBRARY_SUBOBJECT* pHitDesc = desc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
            auto shaderBytecode_hit = CD3DX12_SHADER_BYTECODE(hitShader.GetByteCode(), hitShader.GetByteCodeSize());
            pHitDesc->SetDXILLibrary(&shaderBytecode_hit);
            pHitDesc->DefineExport(L"ClosestHit");

            CD3DX12_DXIL_LIBRARY_SUBOBJECT* pMissDesc = desc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
            auto shaderBytecode_miss = CD3DX12_SHADER_BYTECODE(missShader.GetByteCode(), missShader.GetByteCodeSize());
            pMissDesc->SetDXILLibrary(&shaderBytecode_miss);
            pMissDesc->DefineExport(L"Miss");
        }

        {
            CD3DX12_HIT_GROUP_SUBOBJECT* pHitGroupDesc = desc.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
            pHitGroupDesc->SetHitGroupExport(L"HitGroup");
            pHitGroupDesc->SetClosestHitShaderImport(L"ClosestHit");
        }

        {
            CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT* pRayGenRs = desc.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
            pRayGenRs->SetRootSignature(m_pRayGenSignature->GetRootSignature());
            CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT* pRayGenAssociation = desc.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
            pRayGenAssociation->AddExport(L"RayGen");
            pRayGenAssociation->SetSubobjectToAssociate(*pRayGenRs);

            CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT* pMissRs = desc.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
            pMissRs->SetRootSignature(m_pMissSignature->GetRootSignature());
            CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT* pMissAssociation = desc.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
            pMissAssociation->AddExport(L"Miss");
            pMissAssociation->SetSubobjectToAssociate(*pMissRs);

            CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT* pHitRs = desc.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
            pHitRs->SetRootSignature(m_pHitSignature->GetRootSignature());
            CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT* pHitAssociation = desc.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
            pHitAssociation->AddExport(L"HitGroup");
            pHitAssociation->SetSubobjectToAssociate(*pHitRs);
        }

        {
            CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT* pRtConfig = desc.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
            pRtConfig->Config(4 * sizeof(float), 2 * sizeof(float));

            CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT* pRtPipelineConfig = desc.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
            pRtPipelineConfig->Config(1);

            CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT* pGlobalRs = desc.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
            pGlobalRs->SetRootSignature(m_pDummySignature->GetRootSignature());
        }
        D3D12_STATE_OBJECT_DESC stateObject = *desc;

        ComPtr<ID3D12Device5> pDevice;
        pGraphics->GetDevice()->QueryInterface(IID_PPV_ARGS(pDevice.GetAddressOf()));

        HR(pDevice->CreateStateObject(&stateObject, IID_PPV_ARGS(m_pStateObject.GetAddressOf())));
        HR(m_pStateObject->QueryInterface(IID_PPV_ARGS(m_pStateObjectProperties.GetAddressOf())));
    }
}