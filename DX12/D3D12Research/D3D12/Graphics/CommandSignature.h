#pragma once

class CommandSignature
{
public:
    CommandSignature();
    ~CommandSignature();

    void Finalize(const char* pName, ID3D12Device* pDevice);

    void SetRootSignature(ID3D12RootSignature* pRootSignature) { m_pRootSignature = pRootSignature; };
    void AddDispatch();

    ID3D12CommandSignature* GetCommandSignature() const { return m_pCommandSignature.Get(); }

private:
    // describes the layout of data used in indirect draw or dispatch commamds(e.g. ExecuteIndirect)
    ComPtr<ID3D12CommandSignature> m_pCommandSignature;
    ID3D12RootSignature* m_pRootSignature{nullptr};
    uint32_t m_Stride{0};
    std::vector<D3D12_INDIRECT_ARGUMENT_DESC> m_ArgumentDescs;
};