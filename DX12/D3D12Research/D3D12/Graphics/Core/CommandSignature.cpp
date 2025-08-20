#include "stdafx.h"
#include "CommandSignature.h"

void CommandSignature::Finalize(const char* pName, ID3D12Device* pDevice)
{
    D3D12_COMMAND_SIGNATURE_DESC desc = {};
    desc.ByteStride = m_Stride;
    desc.NumArgumentDescs = static_cast<uint32_t>(m_ArgumentDescs.size());
    desc.pArgumentDescs = m_ArgumentDescs.data();
    desc.NodeMask = 0;

    VERIFY_HR_EX(pDevice->CreateCommandSignature(&desc, m_pRootSignature, IID_PPV_ARGS(m_pCommandSignature.GetAddressOf())), pDevice);
    D3D_SETNAME(m_pCommandSignature.Get(), pName);
}

void CommandSignature::AddDispatch()
{
    D3D12_INDIRECT_ARGUMENT_DESC desc = {};
    desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    m_ArgumentDescs.push_back(desc);
    constexpr int drawArgumentsCount = 3; // 3 uint32_t for x, y, z dimensions
    m_Stride += drawArgumentsCount * sizeof(uint32_t); 
}

void CommandSignature::AddDraw()
{
    D3D12_INDIRECT_ARGUMENT_DESC desc = {};
    desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    m_ArgumentDescs.push_back(desc);
    constexpr int drawArgumentsCount = 4;
    m_Stride += drawArgumentsCount * sizeof(uint32_t);
}

void CommandSignature::AddDrawIndexed()
{
    D3D12_INDIRECT_ARGUMENT_DESC desc;
    desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
    m_ArgumentDescs.push_back(desc);
    constexpr int drawArgumentsCount = 4;
    m_Stride += drawArgumentsCount * sizeof(uint32_t);
}
