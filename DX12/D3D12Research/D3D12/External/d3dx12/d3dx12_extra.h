#pragma once

#include "d3d12.h"
#include <iomanip>

struct CD3DX12_INPUT_ELEMENT_DESC : public D3D12_INPUT_ELEMENT_DESC
{
    CD3DX12_INPUT_ELEMENT_DESC() = default;
    explicit CD3DX12_INPUT_ELEMENT_DESC(const D3D12_INPUT_ELEMENT_DESC& o) noexcept : D3D12_INPUT_ELEMENT_DESC(o) {}

    CD3DX12_INPUT_ELEMENT_DESC(
        const char* semanticName,
        DXGI_FORMAT format,
        uint32_t semanticIndex = 0,
        uint32_t byteOffset = D3D12_APPEND_ALIGNED_ELEMENT,
        uint32_t inputSlot = 0,
        D3D12_INPUT_CLASSIFICATION inputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
        uint32_t instanceDataStepRate = 0) noexcept
    {
        SemanticName = semanticName;
        SemanticIndex = semanticIndex;
        Format = format;
        InputSlot = inputSlot;
        AlignedByteOffset = byteOffset;
        InputSlotClass = inputSlotClass;
        InstanceDataStepRate = instanceDataStepRate;
    }
};

struct CD3DX12_QUERY_HEAP_DESC : public D3D12_QUERY_HEAP_DESC
{
    CD3DX12_QUERY_HEAP_DESC() = default;
    CD3DX12_QUERY_HEAP_DESC(uint32_t count, D3D12_QUERY_HEAP_TYPE type, uint32_t nodeMask = 0)
    {
        Type = type;
        Count = count;
        NodeMask = nodeMask;
    }
};

class CD3DX12_STATE_OBJECT_HELPER
{
	class PODLinearAllocator
	{   
	public:
		PODLinearAllocator(uint32_t size)
			: m_Size(size), m_pData(new uint8_t[m_Size]), m_pCurrentData(m_pData)
		{
			memset(m_pData, 0, m_Size);
		}

		~PODLinearAllocator()
		{
			delete[] m_pData;
			m_pData = nullptr;
		}

		template<typename T>
		T* Allocate(int count = 1)
		{
			return (T*)Allocate(sizeof(T) * count);
		}

		uint8_t* Allocate(uint32_t size)
		{
			check(size > 0);
			checkf(m_pCurrentData - m_pData - size <= m_Size, "make allocator size larger");
			uint8_t* pData = m_pCurrentData;
			m_pCurrentData += size;
			return pData;
		}

		const uint8_t* Data() const
		{
			return m_pData;
		}

	private:
		uint32_t m_Size;
		uint8_t* m_pData;
		uint8_t* m_pCurrentData;
	};

    wchar_t* GetUnicode(const char* pText)
    {
        int len = (int)strlen(pText);
        wchar_t* pNameData = m_ScratchAllocator.Allocate<wchar_t>(len + 1);
        MultiByteToWideChar(0, 0, pText, len, pNameData, len);
        return pNameData;
    }

public:
	CD3DX12_STATE_OBJECT_HELPER(D3D12_STATE_OBJECT_TYPE type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE)
        : m_ScratchAllocator(0xFFFF), m_StateObjectAllocator(0xFF), m_Type(type)
    {}

	uint32_t AddLibrary(const D3D12_SHADER_BYTECODE& byteCode, const char** pInExports, uint32_t numExports)
    {
        D3D12_DXIL_LIBRARY_DESC* pDesc = m_ScratchAllocator.Allocate<D3D12_DXIL_LIBRARY_DESC>();
        pDesc->DXILLibrary = byteCode;
        if (numExports)
        {
            D3D12_EXPORT_DESC* pExports = m_ScratchAllocator.Allocate<D3D12_EXPORT_DESC>(numExports);
            D3D12_EXPORT_DESC* pCurrentExport = pExports;
            for (uint32_t i = 0; i < numExports; i++)
            {
                wchar_t* pNameData = GetUnicode(pInExports[i]);
                pCurrentExport->Name = pNameData;
                pCurrentExport->ExportToRename = pNameData;
                pCurrentExport->Flags = D3D12_EXPORT_FLAG_NONE;
                pCurrentExport++;
            }
            pDesc->NumExports = numExports;
            pDesc->pExports = pExports;
        }
        return AddStateObject(pDesc, D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY);
    }

	uint32_t AddHitGroup(const char* pHitGroupExport, const char* pClosestHigShaderImport = nullptr, 
						 const char* pAnyHitShaderImport = nullptr, const char* pIntersectionShaderImport = nullptr)
    {
        check(pHitGroupExport);
        D3D12_HIT_GROUP_DESC* pDesc = m_ScratchAllocator.Allocate<D3D12_HIT_GROUP_DESC>();
        {
            pDesc->HitGroupExport = GetUnicode(pHitGroupExport);
        }
        if (pClosestHigShaderImport)
        {
            pDesc->ClosestHitShaderImport = GetUnicode(pClosestHigShaderImport);
        }
        if (pAnyHitShaderImport)
        {
            pDesc->AnyHitShaderImport = GetUnicode(pAnyHitShaderImport);
        }
        if (pIntersectionShaderImport)
        {
            pDesc->IntersectionShaderImport = GetUnicode(pIntersectionShaderImport);
        }

        pDesc->Type = pIntersectionShaderImport ? D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE : D3D12_HIT_GROUP_TYPE_TRIANGLES;
        return AddStateObject(pDesc, D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP);
    }

	uint32_t AddStateAssociation(uint32_t index, const std::vector<std::string>& exports)
    {
        check(exports.size());
        D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION* pAssociation = m_ScratchAllocator.Allocate<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION>();
        pAssociation->NumExports = (uint32_t)exports.size();
        pAssociation->pSubobjectToAssociate = GetSubobject(index);
        const wchar_t** pExportList = m_ScratchAllocator.Allocate<const wchar_t*>(pAssociation->NumExports);
        pAssociation->pExports = pExportList;
        for (size_t i = 0; i < exports.size(); i++)
        {
            pExportList[i] = GetUnicode(exports[i].c_str());
        }
        return AddStateObject(pAssociation, D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION);
    }

	uint32_t AddCollection(ID3D12StateObject* pStateObject, const std::vector<std::string>& exports = {})
    {
        D3D12_EXISTING_COLLECTION_DESC* pDesc = m_ScratchAllocator.Allocate<D3D12_EXISTING_COLLECTION_DESC>();
        pDesc->pExistingCollection = pStateObject;
        if (exports.size())
        {
            D3D12_EXPORT_DESC* pExports = m_ScratchAllocator.Allocate<D3D12_EXPORT_DESC>((uint32_t)exports.size());
            D3D12_EXPORT_DESC* pCurrentExport = pExports;
            for (const std::string& exportName : exports)
            {
                wchar_t* pNameData = GetUnicode(exportName.c_str());
                pCurrentExport->Name = pNameData;
                pCurrentExport->ExportToRename = pNameData;
                pCurrentExport->Flags = D3D12_EXPORT_FLAG_NONE;
                pCurrentExport++;
            }
            pDesc->NumExports = (uint32_t)exports.size();
            pDesc->pExports = pExports;
        }
        return AddStateObject(pDesc, D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION);
    }

	uint32_t BindLocalRootSignature(const char* pExportName, ID3D12RootSignature* pRootSignature)
    {
        D3D12_LOCAL_ROOT_SIGNATURE* pRS = m_ScratchAllocator.Allocate<D3D12_LOCAL_ROOT_SIGNATURE>();
        pRS->pLocalRootSignature = pRootSignature;
        uint32_t rsState = AddStateObject(pRS, D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE);
        return AddStateAssociation(rsState, { pExportName });
    }

	uint32_t SetRaytracingShaderConfig(uint32_t maxPayloadSize, uint32_t maxAttributeSize)
    {
        D3D12_RAYTRACING_SHADER_CONFIG* pConfig = m_ScratchAllocator.Allocate<D3D12_RAYTRACING_SHADER_CONFIG>();
        pConfig->MaxPayloadSizeInBytes = maxPayloadSize;
        pConfig->MaxAttributeSizeInBytes = maxAttributeSize;
        return AddStateObject(pConfig, D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG);
    }

	uint32_t SetRaytracingPipelineConfig(uint32_t maxRecursionDepth)
    {
        D3D12_RAYTRACING_PIPELINE_CONFIG1* pConfig = m_ScratchAllocator.Allocate<D3D12_RAYTRACING_PIPELINE_CONFIG1>();
        pConfig->MaxTraceRecursionDepth = maxRecursionDepth;
        return AddStateObject(pConfig, D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG);
    }

	uint32_t SetGlobalRootSignature(ID3D12RootSignature* pRootSignature)
    {
        D3D12_GLOBAL_ROOT_SIGNATURE* pRS = m_ScratchAllocator.Allocate<D3D12_GLOBAL_ROOT_SIGNATURE>();
        pRS->pGlobalRootSignature = pRootSignature;
        return AddStateObject(pRS, D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE);
    }

	void SetStateObjectConfig(D3D12_STATE_OBJECT_FLAGS flags)
    {
        D3D12_STATE_OBJECT_CONFIG* pConfig = m_ScratchAllocator.Allocate<D3D12_STATE_OBJECT_CONFIG>();
        pConfig->Flags = flags;
        AddStateObject(pConfig, D3D12_STATE_SUBOBJECT_TYPE_STATE_OBJECT_CONFIG);
    }

    D3D12_STATE_OBJECT_DESC Desc()
    {
        D3D12_STATE_OBJECT_DESC desc = {
            .Type = m_Type,
            .NumSubobjects = m_SubObjects,
            .pSubobjects = (D3D12_STATE_SUBOBJECT*)m_StateObjectAllocator.Data()
        };
        return desc;
    }

private:
	uint32_t AddStateObject(void* pDesc, D3D12_STATE_SUBOBJECT_TYPE type)
    {
        D3D12_STATE_SUBOBJECT* pSubObject = m_StateObjectAllocator.Allocate<D3D12_STATE_SUBOBJECT>();
        pSubObject->Type = type;
        pSubObject->pDesc = pDesc;
        return m_SubObjects++;
    }

	const D3D12_STATE_SUBOBJECT* GetSubobject(uint32_t index) const
	{
		checkf(index < m_SubObjects, "index out of bounds");
		const D3D12_STATE_SUBOBJECT* pData = (D3D12_STATE_SUBOBJECT*)m_StateObjectAllocator.Data();
		return &pData[index];
	}

	PODLinearAllocator m_StateObjectAllocator;
	PODLinearAllocator m_ScratchAllocator;
	uint32_t m_SubObjects = 0;
	D3D12_STATE_OBJECT_TYPE m_Type;
};

static std::string DebugPrint(const D3D12_STATE_OBJECT_DESC* desc)
{
    std::wstringstream wstr;
    wstr << L"----------------------------------------------------------\n";
    wstr << L"| D3D12 State Object 0x" << static_cast<const void*>(desc) << L": ";
    if (desc->Type == D3D12_STATE_OBJECT_TYPE_COLLECTION) wstr << L"Collection\n";
    if (desc->Type == D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE) wstr << L"Raytracing Pipeline\n";

    auto ExportTree = [](UINT depth, UINT numExports, const D3D12_EXPORT_DESC* exports)
    {
        std::wostringstream woss;
        for (UINT i = 0; i < numExports; i++)
        {
            woss << L"|";
            if (depth > 0)
            {
                for (UINT j = 0; j < 2 * depth - 1; j++) woss << L" ";
            }
            woss << L" [" << i << L"]: ";
            if (exports[i].ExportToRename) woss << exports[i].ExportToRename << L" --> ";
            woss << exports[i].Name << L"\n";
        }
        return woss.str();
    };

    for (UINT i = 0; i < desc->NumSubobjects; i++)
    {
        wstr << L"| [" << i << L"]: ";
        switch (desc->pSubobjects[i].Type)
        {
            case D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE:
            {
                wstr << L"Global Root Signature 0X" << desc->pSubobjects[i].pDesc << L"\n";
                break;
            }
            case D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE:
            {
                wstr << L"Local Root Signature 0X" << desc->pSubobjects[i].pDesc << L"\n";
                break;
            }
            case D3D12_STATE_SUBOBJECT_TYPE_NODE_MASK:
            {
                wstr << L"Node Mask: 0x" << std::hex << std::setfill(L'0') << std::setw(8) 
                    << *static_cast<const UINT*>(desc->pSubobjects[i].pDesc) << std::setw(0) << std::dec << L"\n";
                break;
            }
            case D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY:
            {
                wstr << L"DXIL Library 0x";
                auto pLibDesc = static_cast<const D3D12_DXIL_LIBRARY_DESC*>(desc->pSubobjects[i].pDesc);
                wstr << pLibDesc->DXILLibrary.pShaderBytecode << L", " 
                     << pLibDesc->DXILLibrary.BytecodeLength << L" bytes\n";
                wstr << ExportTree(1, pLibDesc->NumExports, pLibDesc->pExports);
                break;
            }
            case D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION:
            {
                wstr << L"Existing Library 0x";
                auto pColDesc = static_cast<const D3D12_EXISTING_COLLECTION_DESC*>(desc->pSubobjects[i].pDesc);
                wstr << pColDesc->pExistingCollection << L"\n";
                wstr << ExportTree(1, pColDesc->NumExports, pColDesc->pExports);
                break;
            }
            case D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
            {
                wstr << L"Subobject to Exports Association (Subobject ["; 
                auto pAssocDesc = static_cast<const D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION*>(desc->pSubobjects[i].pDesc);
                UINT index = static_cast<UINT>(pAssocDesc->pSubobjectToAssociate - desc->pSubobjects);
                wstr << index << L"])\n";
                for (UINT j = 0; j < pAssocDesc->NumExports; j++)
                {
                    wstr << L"| [" << j << L"]: " << pAssocDesc->pExports[j] << L"\n";
                }
                break;
            }
            case D3D12_STATE_SUBOBJECT_TYPE_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
            {
                wstr << L"DXIL Subobject to Exports Association (";
                auto pAssocDesc = static_cast<const D3D12_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION*>(desc->pSubobjects[i].pDesc);
                wstr << pAssocDesc->SubobjectToAssociate << L")\n";
                for (UINT j = 0; j < pAssocDesc->NumExports; j++)
                {
                    wstr << L"| [" << j << L"]: " << pAssocDesc->pExports[j] << L"\n";
                }
                break;
            }
            case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG:
            {
                wstr << L"Raytracing Shader Config\n";
                auto pConfigDesc = static_cast<const D3D12_RAYTRACING_SHADER_CONFIG*>(desc->pSubobjects[i].pDesc);
                wstr << L"| [0]: Max Payload Size: " << pConfigDesc->MaxPayloadSizeInBytes << L" bytes\n";
                wstr << L"| [1]: Max Attribute Size: " << pConfigDesc->MaxAttributeSizeInBytes << L" bytes\n";
                break;
            }
            case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG:
            {
                wstr << L"Raytracing Pipeline Config\n";
                auto pConfigDesc = static_cast<const D3D12_RAYTRACING_PIPELINE_CONFIG1*>(desc->pSubobjects[i].pDesc);
                wstr << L"| [0]: Max Recursion Depth: " << pConfigDesc->MaxTraceRecursionDepth << L"\n";
                break;
            }
            case D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP:
            {
                wstr << L"Hit Group (";
                auto pHitGroupDesc = static_cast<const D3D12_HIT_GROUP_DESC*>(desc->pSubobjects[i].pDesc);
                wstr << (pHitGroupDesc->HitGroupExport ? pHitGroupDesc->HitGroupExport : L"[none]") << L")\n";
                wstr << L"| [0]: Any Hit Shader Import: " << (pHitGroupDesc->AnyHitShaderImport ? pHitGroupDesc->AnyHitShaderImport : L"[none]") << L"\n";
                wstr << L"| [1]: Closest Hit Shader Import: " << (pHitGroupDesc->ClosestHitShaderImport ? pHitGroupDesc->ClosestHitShaderImport : L"[none]") << L"\n";
                wstr << L"| [2]: Intersection Shader Import: " << (pHitGroupDesc->IntersectionShaderImport ? pHitGroupDesc->IntersectionShaderImport : L"[none]") << L"\n";
                break;
            }
        }
        wstr << L"----------------------------------------------------------\n";
    }
    std::wstring woutput = wstr.str();

    size_t size = 0;
    wcstombs_s(&size, nullptr, 0, woutput.c_str(), 4096);
    char* aOutput = new char[size];
    wcstombs_s(&size, aOutput, size, woutput.c_str(), 4096);
    std::string result = aOutput;
    delete[] aOutput;
    return result;
}