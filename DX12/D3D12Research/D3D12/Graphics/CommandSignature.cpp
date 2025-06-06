#include "stdafx.h"
#include "CommandSignature.h"

CommandSignature::CommandSignature()
{

}

CommandSignature::~CommandSignature()
{

}

void CommandSignature::Finalize(const char* pName, ID3D12Device* pDevice)
{
    D3D12_COMMAND_SIGNATURE_DESC desc = {};
    desc.ByteStride = m_Stride;
    desc.NumArgumentDescs = static_cast<uint32_t>(m_ArgumentDescs.size());
    desc.pArgumentDescs = m_ArgumentDescs.data();
    desc.NodeMask = 0;

    HR(pDevice->CreateCommandSignature(&desc, m_pRootSignature, IID_PPV_ARGS(m_pCommandSignature.GetAddressOf())));
    SetD3DObjectName(m_pCommandSignature.Get(), pName);
}

void CommandSignature::AddDispatch()
{
    D3D12_INDIRECT_ARGUMENT_DESC desc = {};
    desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    m_ArgumentDescs.push_back(desc);
    m_Stride += 3 * sizeof(uint32_t); // 3 uint32_t for x, y, z dimensions
}