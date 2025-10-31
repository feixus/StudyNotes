#include "stdafx.h"
#include "StateObject.h"
#include "Shader.h"
#include "RootSignature.h"
#include "Graphics.h"

StateObject::StateObject(Graphics* pGraphics) : GraphicsObject(pGraphics)
{}

void StateObject::Create(const StateObjectInitializer& initializer)
{
    m_Desc = initializer;

    D3D12_STATE_OBJECT_DESC desc = m_Desc.Desc();
    VERIFY_HR(GetGraphics()->GetRaytracingDevice()->CreateStateObject(&desc, IID_PPV_ARGS(m_pStateObject.ReleaseAndGetAddressOf())));
    D3D::SetObjectName(m_pStateObject.Get(), m_Desc.Name.c_str());
}

void StateObjectInitializer::AddHitGroup(const std::string& name, const std::string& closestHit, const std::string& anyHit, const std::string& intersection, RootSignature* pRootSignature)
{
	HitGroupDefinition definition;
    definition.Name = name;
    definition.ClosestHit = closestHit;
    definition.AnyHit = anyHit;
    definition.Intersection = intersection;
    definition.pLocalRootSignature = pRootSignature;
    m_HitGroups.push_back(definition);
}

void StateObjectInitializer::AddLibrary(ShaderLibrary* pLibrary, const std::vector<std::string>& exports)
{
	LibraryExports libraryExports;
    libraryExports.pLibrary = pLibrary;
    libraryExports.Exports = exports;
    m_Libraries.push_back(libraryExports);
}

void StateObjectInitializer::AddCollection(StateObject* pOtherObject)
{
    m_Collections.push_back(pOtherObject);
}

void StateObjectInitializer::AddMissShader(const std::string& name, RootSignature* pRootSignature)
{
    LibraryShaderExport missShader;
    missShader.Name = name;
    missShader.pLocalRootSignature = pRootSignature;
    m_MissShaders.push_back(missShader);
}

void StateObjectInitializer::SetRayGenShader(const std::string& name)
{
    RayGenShader = name;
}

D3D12_STATE_OBJECT_DESC StateObjectInitializer::Desc()
{
    m_StateObjectData.Reset();
    m_ContentData.Reset();

    uint32_t numObjects = 0;
    auto AddStateObject = [this, &numObjects](void* pDesc, D3D12_STATE_SUBOBJECT_TYPE type)
    {
        D3D12_STATE_SUBOBJECT* pSubObject = m_StateObjectData.Allocate<D3D12_STATE_SUBOBJECT>();
        pSubObject->Type = type;
        pSubObject->pDesc = pDesc;
        ++numObjects;
        return pSubObject;
    };

    for (const LibraryExports& library : m_Libraries)
    {
        D3D12_DXIL_LIBRARY_DESC* pDesc = m_ContentData.Allocate<D3D12_DXIL_LIBRARY_DESC>();
        pDesc->DXILLibrary = CD3DX12_SHADER_BYTECODE(library.pLibrary->GetByteCode(), library.pLibrary->GetByteCodeSize());
        if (library.Exports.size())
        {
            D3D12_EXPORT_DESC* pExports = m_ContentData.Allocate<D3D12_EXPORT_DESC>((int)library.Exports.size());
            D3D12_EXPORT_DESC* pCurrentExport = pExports;
            for (uint32_t i = 0; i < library.Exports.size(); i++)
            {
                wchar_t* pNameData = GetUnicode(library.Exports[i]);
                pCurrentExport->Name = pNameData;
                pCurrentExport->ExportToRename = pNameData;
                pCurrentExport->Flags = D3D12_EXPORT_FLAG_NONE;
                pCurrentExport++;
            }
            pDesc->NumExports = (uint32_t)library.Exports.size();
            pDesc->pExports = pExports;
        }
        AddStateObject(pDesc, D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY);
    }

    for (const HitGroupDefinition& hitGroup : m_HitGroups)
    {
        check(!hitGroup.Name.empty());
        D3D12_HIT_GROUP_DESC* pDesc = m_ContentData.Allocate<D3D12_HIT_GROUP_DESC>();
        pDesc->HitGroupExport = GetUnicode(hitGroup.Name);

        if (!hitGroup.ClosestHit.empty())
        {
            pDesc->ClosestHitShaderImport = GetUnicode(hitGroup.ClosestHit);
        }
        if (!hitGroup.AnyHit.empty())
        {
            pDesc->AnyHitShaderImport = GetUnicode(hitGroup.AnyHit);
        }
        if (!hitGroup.Intersection.empty())
        {
            pDesc->IntersectionShaderImport = GetUnicode(hitGroup.Intersection);
        }

        pDesc->Type = !hitGroup.Intersection.empty() ? D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE : D3D12_HIT_GROUP_TYPE_TRIANGLES;
        AddStateObject(pDesc, D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP);

        if (hitGroup.pLocalRootSignature)
        {
            D3D12_LOCAL_ROOT_SIGNATURE* pRS = m_ContentData.Allocate<D3D12_LOCAL_ROOT_SIGNATURE>();
            pRS->pLocalRootSignature = hitGroup.pLocalRootSignature->GetRootSignature();
            D3D12_STATE_SUBOBJECT* pSubObject = AddStateObject(pRS, D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE);
            
            D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION* pAssociation = m_ContentData.Allocate<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION>();
            pAssociation->NumExports = 1;
            pAssociation->pSubobjectToAssociate = pSubObject;
            const wchar_t** pExportList = m_ContentData.Allocate<const wchar_t*>(pAssociation->NumExports);
            pExportList[0] = GetUnicode(hitGroup.Name);
            pAssociation->pExports = pExportList;
            AddStateObject(pAssociation, D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION);
        }
    }

    for (const LibraryShaderExport& missShader : m_MissShaders)
    {
        if (missShader.pLocalRootSignature)
        {
            D3D12_LOCAL_ROOT_SIGNATURE* pRS = m_ContentData.Allocate<D3D12_LOCAL_ROOT_SIGNATURE>();
            pRS->pLocalRootSignature = missShader.pLocalRootSignature->GetRootSignature();
            D3D12_STATE_SUBOBJECT* pSubObject = AddStateObject(pRS, D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE);
            
            D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION* pAssociation = m_ContentData.Allocate<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION>();
            pAssociation->NumExports = 1;
            pAssociation->pSubobjectToAssociate = pSubObject;
            const wchar_t** pExportList = m_ContentData.Allocate<const wchar_t*>(pAssociation->NumExports);
            pExportList[0] = GetUnicode(missShader.Name);
            pAssociation->pExports = pExportList;
            AddStateObject(pAssociation, D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION);
        }
    }

    if (Flags != D3D12_RAYTRACING_PIPELINE_FLAG_NONE)
    {
        D3D12_RAYTRACING_PIPELINE_CONFIG1* pPipelineConfig = m_ContentData.Allocate<D3D12_RAYTRACING_PIPELINE_CONFIG1>();
        pPipelineConfig->MaxTraceRecursionDepth = MaxRecursion;
        pPipelineConfig->Flags = Flags;
        AddStateObject(pPipelineConfig, D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1);
    }
    else
    {
        D3D12_RAYTRACING_PIPELINE_CONFIG1* pPipelineConfig = m_ContentData.Allocate<D3D12_RAYTRACING_PIPELINE_CONFIG1>();
        pPipelineConfig->MaxTraceRecursionDepth = MaxRecursion;
        AddStateObject(pPipelineConfig, D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG);
    }

    D3D12_RAYTRACING_SHADER_CONFIG* pShaderConfig = m_ContentData.Allocate<D3D12_RAYTRACING_SHADER_CONFIG>();
    pShaderConfig->MaxPayloadSizeInBytes = MaxPayloadSize;
    pShaderConfig->MaxAttributeSizeInBytes = MaxAttributeSize;
    AddStateObject(pShaderConfig, D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG);

    D3D12_GLOBAL_ROOT_SIGNATURE* pRS = m_ContentData.Allocate<D3D12_GLOBAL_ROOT_SIGNATURE>();
    pRS->pGlobalRootSignature = pGlobalRootSignature->GetRootSignature();
    AddStateObject(pRS, D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE);

    D3D12_STATE_OBJECT_DESC desc;
    desc.Type = Type;
    desc.NumSubobjects = numObjects;
    desc.pSubobjects = reinterpret_cast<const D3D12_STATE_SUBOBJECT*>(m_StateObjectData.GetData());
    return desc;
}
