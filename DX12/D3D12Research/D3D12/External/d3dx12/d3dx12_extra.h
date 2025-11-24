#pragma once

#include "d3d12.h"
#include <iomanip>

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
