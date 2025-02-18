#pragma once
#include <stdint.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include "GraphicsManager.hpp"

namespace My
{
    class D3d12GraphicsManager : public GraphicsManager
    {
    public:
        virtual int Initialize();
        virtual void Finalize();

        virtual void Tick();

    private:
        HRESULT CreateRenderTarget();
        HRESULT CreateGraphicsResources();

    private:
        static const uint32_t kFrameCount = 2;

        ID3D12Device*           m_pDev {};
        IDXGISwapChain4*        m_pSwapChain {};
        ID3D12Resource*         m_pRenderTargets[kFrameCount];
        ID3D12CommandAllocator* m_pCommandAllocator {};
        ID3D12CommandQueue*     m_pCommandQueue {};
        ID3D12RootSignature*    m_pRootSignature {};  // a graphics root signature defines what resources are bound to the pipeline
        ID3D12DescriptorHeap*   m_pRtvHeap {};        // an array of descriptors of GPU objects
        ID3D12PipelineState*    m_pPipelineState {};  // an object maintains the state of all currently  set shaders
                                                            // and certain fixed function state objects
                                                            // such as the input assembler, tesselator, rasterizer and output manager

        ID3D12GraphicsCommandList* m_pCommandList {}; // a list to store GPU commands, which will be submitted to GPU to execute when done

        uint32_t m_nRtvDescriptorSize;
        uint32_t m_nCbvSrvDescriptorSize;

        ID3D12Resource*           m_pVertexBuffer {};
        D3D12_VERTEX_BUFFER_VIEW  m_VertexBufferView;

        // synchronization objects
        uint32_t            m_nFrameIndex;
        HANDLE              m_hFenceEvent;
        ID3D12Fence*        m_pFence {};
        uint16_t            m_nFenceValue;  
    };
}