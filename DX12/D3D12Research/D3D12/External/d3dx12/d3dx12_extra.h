#pragma once

#include "d3d12.h"

#if defined( __cplusplus)

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
        uint32_t instanceDataStepRate = 0)
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

#endif