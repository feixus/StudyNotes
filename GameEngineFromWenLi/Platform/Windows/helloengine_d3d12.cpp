#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <tchar.h>
#include <stdint.h>
#include <cmath>

#include <d3d12.h>
#include "directx/d3dx12.h"
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <DirectXColors.h>

#include <wrl/client.h>

#include <string>
#include <exception>

#include "Mesh.h"

namespace My
{
    class com_exception : public std::exception
    {
    public:
        com_exception(HRESULT hr) : result(hr) {}

        virtual const char* what() const override
        {
            static char s_str[64] = { 0 };
            sprintf_s(s_str, "Failure with HRESULT of %08X", static_cast<unsigned int>(result));
            return s_str;
        }
    private:
        HRESULT result;
    };

    inline void ThrowIfFailed(HRESULT hr)
    {
        if (FAILED(hr))
        {
            throw com_exception(hr);
        }
    }
}

using namespace My;
using namespace DirectX;
using namespace DirectX::PackedVector;
using namespace Microsoft::WRL;
using namespace std;

const uint32_t nScreenWidth = 960;
const uint32_t nScreenHeight = 480;

const uint32_t nFrameCount = 2;


//global declarations
D3D12_VIEWPORT g_ViewPort = {0.0f, 0.0f, static_cast<float>(nScreenWidth), static_cast<float>(nScreenHeight)};
D3D12_RECT     g_ScissorRect = {0, 0, nScreenWidth, nScreenHeight};

ComPtr<IDXGISwapChain4>        g_pSwapChain = nullptr;
ComPtr<ID3D12Device>           g_pDev = nullptr;
ComPtr<ID3D12Resource>         g_pRenderTargets[nFrameCount];
ComPtr<ID3D12CommandAllocator> g_pCommandAllocator;
ComPtr<ID3D12CommandQueue>     g_pCommandQueue;
ComPtr<ID3D12RootSignature>    g_pRootSignature;  // a graphics root signature defines what resources are bound to the pipeline

ComPtr<ID3D12DescriptorHeap>   g_pRtvHeap;        // an array of descriptors of GPU objects
ComPtr<ID3D12DescriptorHeap>   g_pDsvHeap;         
ComPtr<ID3D12DescriptorHeap>   g_pCbvSrvHeap;
ComPtr<ID3D12DescriptorHeap>   g_pSamplerHeap;

ComPtr<ID3D12PipelineState>    g_pPipelineState;  // an object maintains the state of all currently  set shaders
                                                    // and certain fixed function state objects
                                                    // such as the input assembler, tesselator, rasterizer and output manager
ComPtr<ID3D12GraphicsCommandList> g_pCommandList; // a list to store GPU commands, which will be submitted to GPU to execute when done

uint32_t g_nRtvDescriptorSize;
uint32_t g_nCbvSrvDescriptorSize;

ComPtr<ID3D12Resource>          g_pVertexBuffer;
D3D12_VERTEX_BUFFER_VIEW        g_VertexBufferView;
ComPtr<ID3D12Resource>          g_pIndexBuffer;
D3D12_INDEX_BUFFER_VIEW         g_IndexBufferView;
ComPtr<ID3D12Resource>          g_pTextureBuffer;
ComPtr<ID3D12Resource>          g_pDepthStencilBuffer;
ComPtr<ID3D12Resource>          g_pConstantUploadBuffer;

struct SimpleConstants
{
    XMFLOAT4X4 m_modelMatrix;
    XMFLOAT4X4 m_viewMatrix;
    XMFLOAT4X4 m_projectionMatrix;
    XMFLOAT4 m_lightPosition;
    XMFLOAT4 m_lightColor;
    XMFLOAT4 m_ambientColor;
    XMFLOAT4 m_lightAttenuation;
};

uint8_t *g_pCbvDataBegin = nullptr;
SimpleConstants g_ConstantBufferData;

// synchronization objects
uint32_t            g_nFrameIndex;
HANDLE              g_hFenceEvent;
ComPtr<ID3D12Fence> g_pFence;
uint16_t            g_nFenceValue;

// vertex properties
typedef enum VertexElements
{
    kVertexPosition = 0,
    kVertexNormal,
    kVertexTangent,
    kVertexUv,
    kVertexElemCount
} VertexElements;

//vertex buffer structure
struct SimpleMeshVertex
{
    XMFLOAT3 m_position;
    XMFLOAT3 m_normal;
    XMFLOAT3 m_tangent;
    XMFLOAT2 m_uv;
};

SimpleMesh torus;

// matrix
XMMATRIX g_mWorldMatrix;
XMMATRIX g_mViewMatrix;
XMMATRIX g_mProjectionMatrix;

void InitConstants()
{
    const XMVECTOR lightPositionX = XMVectorSet(-1.5f, 4.0f, 9.0f, 1.0f);
    const XMVECTOR lightTargetX = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    const XMVECTOR lightUpX = XMVectorSet(0.0f, 1.0f, 0.0f, 1.0f);

    const float g_depthNear = 1.0f;
    const float g_depthFar = 100.0f;
    const float aspect = static_cast<float>(nScreenWidth) / static_cast<float>(nScreenHeight);
    g_mProjectionMatrix = XMMatrixPerspectiveOffCenterRH(-aspect, aspect, -1.0, 1.0f, g_depthNear, g_depthFar);

    const XMVECTOR eyePos = XMVectorSet(0.0f, 0.0f, 2.5f, 1.0f);
    const XMVECTOR lookAtPos = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    const XMVECTOR upVec = XMVectorSet(0.0f, 1.0f, 0.0f, 1.0f);
    g_mViewMatrix = XMMatrixLookAtRH(eyePos, lookAtPos, upVec);
}

void BuildTorusMesh(
        float outerRadius, float innerRadius,
        uint16_t outerQuads, uint16_t innerQuads,
        float outerRepeats, float innerRepeats,
        SimpleMesh *pDestMesh)
{
    const uint32_t outerVertices = outerQuads + 1;
    const uint32_t innerVertices = innerQuads + 1;
    const uint32_t vertices = outerVertices * innerVertices;
    const uint32_t numInnerQuadsFullStriped = 1;
    const uint32_t innerQuadsLastStripe = 0;
    const uint32_t triangle = 2 * outerQuads * innerQuads;      // 2 triangle per quad

    pDestMesh->m_vertexCount = vertices;
    pDestMesh->m_vertexStride = sizeof(SimpleMeshVertex);
    pDestMesh->m_vertexAttributeCount = kVertexElemCount;
    pDestMesh->m_vertexBufferSize = pDestMesh->m_vertexCount * pDestMesh->m_vertexStride;

    pDestMesh->m_indexCount = triangle * 3;                  // 3 vertices per triangle
    pDestMesh->m_indexType = IndexSize::kIndexSize16;       // whenever possible use smaller index to save memory and enhance cache performance

    pDestMesh->m_primitiveType = PrimitiveType::kPrimitiveTypeTriList;
    pDestMesh->m_indexBufferSize = pDestMesh->m_indexCount * sizeof(uint16_t);

    // build vertices
    pDestMesh->m_vertexBuffer = new uint8_t[pDestMesh->m_vertexBufferSize];

    SimpleMeshVertex *outV = static_cast<SimpleMeshVertex*>(pDestMesh->m_vertexBuffer);
    const XMFLOAT2 textureScale = XMFLOAT2(outerRepeats / (outerVertices - 1), innerRepeats / (innerVertices - 1));
    for (uint32_t o = 0; o < outerVertices; ++o)
    {
        const float outerTheta = o * 2 * XM_PI / (outerVertices - 1);
        const XMMATRIX outerToWorld = XMMatrixTranslation(outerRadius, 0, 0) * XMMatrixRotationZ(outerTheta);

        for (uint32_t i = 0; i < innerVertices; ++i)
        {
            const float innerTheta = i * 2 * XM_PI / (innerVertices - 1);
            const XMMATRIX innerToOuter = XMMatrixTranslation(innerRadius, 0, 0) * XMMatrixRotationY(innerTheta);
            const XMMATRIX localToWorld = innerToOuter * outerToWorld;

            XMVECTOR v = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
            v = XMVector4Transform(v, localToWorld);
            XMStoreFloat3(&outV->m_position, v);

            v = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
            v = XMVector4Transform(v, localToWorld);
            XMStoreFloat3(&outV->m_normal, v);

            v = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
            v = XMVector4Transform(v, localToWorld);
            XMStoreFloat3(&outV->m_tangent, v);

            outV->m_uv.x = o * textureScale.x;
            outV->m_uv.y = i * textureScale.y;

            ++outV;
        }
    }

    // build indices
    pDestMesh->m_indexBuffer = new uint8_t[pDestMesh->m_indexBufferSize];

    uint16_t *outI = static_cast<uint16_t*>(pDestMesh->m_indexBuffer);
    uint16_t const numInnerQuadsStriped = numInnerQuadsFullStriped + (innerQuadsLastStripe > 0 ? 1 : 0);
    for (uint16_t iStripe = 0; iStripe < numInnerQuadsStriped; ++iStripe)
    {
        uint16_t const innerVertex0 = iStripe * innerQuads;

        for (uint16_t o = 0; o < outerQuads; ++o)
        {
            for (uint16_t i = 0; i < innerQuads; ++i)
            {
                const uint16_t index[4] = {
                    static_cast<uint16_t>((o + 0) * innerVertices + innerVertex0 + (i + 0)),
                    static_cast<uint16_t>((o + 0) * innerVertices + innerVertex0 + (i + 1)),
                    static_cast<uint16_t>((o + 1) * innerVertices + innerVertex0 + (i + 0)),
                    static_cast<uint16_t>((o + 1) * innerVertices + innerVertex0 + (i + 1)),
                };

                outI[0] = index[0];
                outI[1] = index[2];
                outI[2] = index[1];
                outI[3] = index[1];
                outI[4] = index[2];
                outI[5] = index[3];
                outI += 6;
            }
        }
    }
}

const uint32_t nTextureWidth = 512;
const uint32_t nTextureHeight = 512;
const uint32_t nTexturePixelSize = 4; // R8G8B8A8

// generate a simple black and white checkerboard texture 
uint8_t* GenerateTextureData()
{
    const uint32_t nRowPitch = nTextureWidth * nTexturePixelSize;
    const uint32_t nCellPitch = nRowPitch >> 3;     // the width and height of a cell in the checkboard texture
    const uint32_t nCellHeight = nTextureWidth >> 3;
    const uint32_t nTextureSize = nRowPitch * nTextureHeight;
    uint8_t* pData = new uint8_t[nTextureSize];

    for (uint32_t n = 0; n < nTextureSize; n += nTexturePixelSize)
    {
        uint32_t x = n % nRowPitch;
        uint32_t y = n / nRowPitch;
        uint32_t i = x / nCellPitch;
        uint32_t j = y / nCellHeight;

        if (i % 2 == j % 2)
        {
            pData[n] = 0x00;    // R
            pData[n + 1] = 0x00;    // G
            pData[n + 2] = 0x00;    // B
            pData[n + 3] = 0xff;    // A
        }
        else
        {
            pData[n] = 0xff;     // R
            pData[n + 1] = 0xff; // G
            pData[n + 2] = 0xff; // B
            pData[n + 3] = 0xff; // A
        }
    }

    return pData;
}

void WaitForPreviousFrame()
{
    // waiting for the frame to complete before continuing is not best practice.
    // this is code implemented as such for simplicity.
    // more advanced samples illustrate how to use fences for efficient resource usage

    // signal and increment the fence value
    const uint64_t fence = g_nFenceValue;
    ThrowIfFailed(g_pCommandQueue->Signal(g_pFence.Get(), fence));
    g_nFenceValue++;

    // wait until the previous frame is finished
    if (g_pFence->GetCompletedValue() < fence)
    {
        ThrowIfFailed(g_pFence->SetEventOnCompletion(fence, g_hFenceEvent));
        WaitForSingleObject(g_hFenceEvent, INFINITE);
    }

    g_nFrameIndex = g_pSwapChain->GetCurrentBackBufferIndex();
}

void CreateDescriptorHeaps()
{
    // describe and create a render target view(RTV) descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = nFrameCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(g_pDev->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&g_pRtvHeap)));

    g_nRtvDescriptorSize = g_pDev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // depth stencil view(DSV) descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(g_pDev->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&g_pDsvHeap)));

    // shader resource view(SRV) and constant buffer view(CBV) descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC csvSrvHeapDesc = {};
    csvSrvHeapDesc.NumDescriptors = nFrameCount + 1;    // frameCount Cbvs, +1 for the Srv(Texture)
    csvSrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    csvSrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(g_pDev->CreateDescriptorHeap(&csvSrvHeapDesc, IID_PPV_ARGS(&g_pCbvSrvHeap)));

    // sampler descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc = {};
    samplerHeapDesc.NumDescriptors = 1;
    samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    samplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;  // the heap available for shaders to use via root signature or descriptor tables, without this flag, just visible to the CPU.
    ThrowIfFailed(g_pDev->CreateDescriptorHeap(&samplerHeapDesc, IID_PPV_ARGS(&g_pSamplerHeap)));

    g_nCbvSrvDescriptorSize = g_pDev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    ThrowIfFailed(g_pDev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_pCommandAllocator)));
}   

void CreateRenderTarget()
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(g_pRtvHeap->GetCPUDescriptorHandleForHeapStart());

    // create a RTV for each frame
    for (uint32_t i = 0; i < nFrameCount; i++)
    {
        ThrowIfFailed(g_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&g_pRenderTargets[i])));
        g_pDev->CreateRenderTargetView(g_pRenderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, g_nRtvDescriptorSize);
    }
}

// load and prepare the shaders
void InitPipeline()
{
    ComPtr<ID3DBlob> error;

    // create the root signature
    {
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

        // this is the highest version the sample supports
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

        if (FAILED(g_pDev->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
        {
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
        }

        CD3DX12_DESCRIPTOR_RANGE1 ranges[3];
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
        ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0);
        ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 6, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

        CD3DX12_ROOT_PARAMETER1 rootParameters[3];
        rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
        rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);
        rootParameters[2].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_ALL);

        // allow input layout and deny uneccessary access to certain pipeline stages
        D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, nullptr, rootSignatureFlags);

        ComPtr<ID3DBlob> signature;
        ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
        ThrowIfFailed(g_pDev->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&g_pRootSignature)));
    }

    // load the shaders
#if defined(_DEBUG_SHADER)
    // enable better shader debugging with the graphics debugging tools
    UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    UINT compileFlags = 0;
#endif

    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> pixelShader;

    D3DCompileFromFile(
        L"simple.hlsl",
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "VSMain",
        "vs_5_0",
        compileFlags,
        0,
        &vertexShader,
        &error);
    if (error)
    {
        OutputDebugString((LPCTSTR)error->GetBufferPointer());
        error->Release();
        throw std::exception();
    }

    D3DCompileFromFile(
        L"simple.hlsl",
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "PSMain",
        "ps_5_0",
        compileFlags,
        0,
        &pixelShader,
        &error);
    if (error)
    {
        OutputDebugString((LPCTSTR)error->GetBufferPointer());
        error->Release();
        throw std::exception();
    }
    
    // create the input layout object
    D3D12_INPUT_ELEMENT_DESC ied[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    // describe and create the graphics pipeline state object (PSO)
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psod = {};
    psod.InputLayout = {ied, _countof(ied)};
    psod.pRootSignature = g_pRootSignature.Get();
    psod.VS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<UINT8*>(vertexShader->GetBufferPointer()), vertexShader->GetBufferSize());
    psod.PS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<UINT8*>(pixelShader->GetBufferPointer()), pixelShader->GetBufferSize());
    psod.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psod.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psod.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psod.SampleMask = UINT_MAX;
    psod.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psod.NumRenderTargets = 1;
    psod.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psod.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psod.SampleDesc.Count = 1;

    ThrowIfFailed(g_pDev->CreateGraphicsPipelineState(&psod, IID_PPV_ARGS(&g_pPipelineState)));

    ThrowIfFailed(g_pDev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_pCommandAllocator.Get(), g_pPipelineState.Get(), IID_PPV_ARGS(&g_pCommandList)));
}

// creates the shape to render
void InitGraphics()
{
    ComPtr<ID3D12Resource2> pVertexBufferUploadHeap;
    ComPtr<ID3D12Resource2> pIndexBufferUploadHeap;
    ComPtr<ID3D12Resource2> pTextureUploadHeap;

    BuildTorusMesh(0.8f, 0.2f, 64, 32, 4, 1, &torus);

    // create vertex buffer
    {
        ThrowIfFailed(g_pDev->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(torus.m_vertexBufferSize),
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&g_pVertexBuffer)));

        ThrowIfFailed(g_pDev->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(torus.m_vertexBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&pVertexBufferUploadHeap)));

        // copy data to the intermidiate upload heap and then schedule a copy from the upload heap to the vertex buffer
        D3D12_SUBRESOURCE_DATA vertexData = {};
        vertexData.pData = torus.m_vertexBuffer;
        vertexData.RowPitch = torus.m_vertexStride;
        vertexData.SlicePitch = vertexData.RowPitch;

        UpdateSubresources<1>(g_pCommandList.Get(), g_pVertexBuffer.Get(), pVertexBufferUploadHeap.Get(), 0, 0, 1, &vertexData);
        g_pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_pVertexBuffer.Get(),
                                                                        D3D12_RESOURCE_STATE_COPY_DEST,
                                                                        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));

        // initialize the vertex buffer view
        g_VertexBufferView.BufferLocation = g_pVertexBuffer->GetGPUVirtualAddress();
        g_VertexBufferView.StrideInBytes = torus.m_vertexStride;
        g_VertexBufferView.SizeInBytes = torus.m_vertexBufferSize;
    }

    // create index buffer
    {
        ThrowIfFailed(g_pDev->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(torus.m_indexBufferSize),
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&g_pIndexBuffer)));

        ThrowIfFailed(g_pDev->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(torus.m_indexBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&pIndexBufferUploadHeap)));

        D3D12_SUBRESOURCE_DATA vertexData = {};
        vertexData.pData = torus.m_indexBuffer;
        vertexData.RowPitch = torus.m_indexType;
        vertexData.SlicePitch = vertexData.RowPitch;

        UpdateSubresources<1>(g_pCommandList.Get(), g_pIndexBuffer.Get(), pIndexBufferUploadHeap.Get(), 0, 0, 1, &vertexData);
        g_pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_pIndexBuffer.Get(),
                                                                        D3D12_RESOURCE_STATE_COPY_DEST,
                                                                        D3D12_RESOURCE_STATE_INDEX_BUFFER));

        g_IndexBufferView.BufferLocation = g_pIndexBuffer->GetGPUVirtualAddress();
        g_IndexBufferView.Format = DXGI_FORMAT_R16_UINT;
        g_IndexBufferView.SizeInBytes = torus.m_indexBufferSize;                                                                        
    }

    // generate the texture
    uint8_t* pTextureData = GenerateTextureData();

    // create the texture and sampler
    {
        // describe and create a Texture2D
        D3D12_RESOURCE_DESC textureDesc = {};
        textureDesc.MipLevels = 1;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.Width = nTextureWidth;
        textureDesc.Height = nTextureHeight;
        textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        textureDesc.DepthOrArraySize = 1;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.SampleDesc.Quality = 0;
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

        ThrowIfFailed(g_pDev->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &textureDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&g_pTextureBuffer)));

        const UINT subresourceCount = textureDesc.DepthOrArraySize * textureDesc.MipLevels;
        const UINT64 uploadBufferSize = GetRequiredIntermediateSize(g_pTextureBuffer.Get(), 0, subresourceCount);

        ThrowIfFailed(g_pDev->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&pTextureUploadHeap)));

        D3D12_SUBRESOURCE_DATA textureData = {};
        textureData.pData = pTextureData;
        textureData.RowPitch = nTextureWidth * nTexturePixelSize;
        textureData.SlicePitch = textureData.RowPitch * nTextureHeight;

        UpdateSubresources(g_pCommandList.Get(), g_pTextureBuffer.Get(), pTextureUploadHeap.Get(), 0, 0, subresourceCount, &textureData);
        g_pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_pTextureBuffer.Get(),
                                                                        D3D12_RESOURCE_STATE_COPY_DEST,
                                                                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));   

        // describe and create a sampler
        D3D12_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.MinLOD = 0;
        samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
        samplerDesc.MipLODBias = 0.0f;
        samplerDesc.MaxAnisotropy = 1;
        samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        g_pDev->CreateSampler(&samplerDesc, g_pSamplerHeap->GetCPUDescriptorHandleForHeapStart());

        // describe and create a SRV for the texture
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(g_pCbvSrvHeap->GetCPUDescriptorHandleForHeapStart());
        g_pDev->CreateShaderResourceView(g_pTextureBuffer.Get(), &srvDesc, srvHandle);
    }

    // create the depth stencil view
    {
        D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};
        depthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT;
        depthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        depthStencilDesc.Flags = D3D12_DSV_FLAG_NONE;

        D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
        depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
        depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
        depthOptimizedClearValue.DepthStencil.Stencil = 0;

        ThrowIfFailed(g_pDev->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, nScreenWidth, nScreenHeight, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &depthOptimizedClearValue,
            IID_PPV_ARGS(&g_pDepthStencilBuffer)));

        g_pDev->CreateDepthStencilView(g_pDepthStencilBuffer.Get(), &depthStencilDesc, g_pDsvHeap->GetCPUDescriptorHandleForHeapStart());
    }

    // create the constant buffer
    {
        size_t sizeConstantBuffer = (sizeof(SimpleConstants) + 255) & ~255; // CB size is required to be 256-byte aligned
        ThrowIfFailed(g_pDev->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(sizeConstantBuffer),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&g_pConstantUploadBuffer)));

        for (uint32_t i = 0; i < nFrameCount; ++i)
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE cbvHandle(g_pCbvSrvHeap->GetCPUDescriptorHandleForHeapStart(), i + 1, g_nCbvSrvDescriptorSize);
            // describe and create a constant buffer view
            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
            cbvDesc.BufferLocation = g_pConstantUploadBuffer->GetGPUVirtualAddress();
            cbvDesc.SizeInBytes = sizeConstantBuffer;
            g_pDev->CreateConstantBufferView(&cbvDesc, cbvHandle);
        }

        // map and initialize the constant buffer. dont unmap this until the app closes.
        // keeping things mapped for the lifetime of the resource is okay.
        CD3DX12_RANGE readRange(0, 0); // dont intend to read from this resource on the GPU
        ThrowIfFailed(g_pConstantUploadBuffer->Map(0, &readRange, reinterpret_cast<void **>(&g_pCbvDataBegin)));
    }

    // close the command list and execute it to begin the initial GPU setup
    ThrowIfFailed(g_pCommandList->Close());
    ID3D12CommandList* ppCommandLists[] = {g_pCommandList.Get()};
    g_pCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // create synchronization objects and wait until assets have been uploaded to the GPU
    ThrowIfFailed(g_pDev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_pFence)));
    g_nFenceValue = 1;

    // create an event handle to use for frame synchronization
    g_hFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (g_hFenceEvent == nullptr)
    {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    }
   
   // wait for the command list to execute
   // reusing the same command list in our main loop but for now, just want to wait for setup to complete before continuing
   WaitForPreviousFrame();

   // the texture is uploaded so we can free the origin data
   delete[] pTextureData;
}

void GetHardwareAdapter(IDXGIFactory7* pFactory, IDXGIAdapter1** ppAdapter)
{
    IDXGIAdapter1* pAdapter = nullptr;
    *ppAdapter = nullptr;

    for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters1(adapterIndex, &pAdapter); ++adapterIndex)
    {
        DXGI_ADAPTER_DESC1 desc;
        pAdapter->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            // dont select the basic render driver adapter
            continue;
        }

        // check to see if the adapter supports Direct3D 12, but dont create the actual device yet. 
        if (SUCCEEDED(D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_12_0, _uuidof(ID3D12Device), nullptr)))
        {
            break;
        }
    }

    *ppAdapter = pAdapter;
}

// prepare graphic resources
void CreateGraphicsResources(HWND hWnd)
{
    if (g_pSwapChain.Get() == nullptr)
    {
#if defined(_DEBUG)
        // enable the D3D12 debug layer
        {
            ComPtr<ID3D12Debug> debugController;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
            {
                debugController->EnableDebugLayer();
            }
        }
#endif

        ComPtr<IDXGIFactory7> factory;
        ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

        ComPtr<IDXGIAdapter1> hardwareAdapter;
        GetHardwareAdapter(factory.Get(), &hardwareAdapter);

        if (FAILED(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&g_pDev))))
        {
            fprintf(stderr, "hardware not support D3D12, fallback to warp device. \n");

            // roll back to Warp device(Windows Advanced Rasterization Platform)
            ComPtr<IDXGIAdapter4> warpAdapter;
            ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

            ThrowIfFailed(D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&g_pDev)));
        }
        
        // describe and create the command queue
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

        ThrowIfFailed(g_pDev->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&g_pCommandQueue)));

        // create a struct to hold information about the swap chain
        DXGI_SWAP_CHAIN_DESC1 scd;

        // clear out the struct for use
        ZeroMemory(&scd, sizeof(DXGI_SWAP_CHAIN_DESC1));

        // fill the swap chain description struct
        scd.BufferCount = nFrameCount;              // back buffer count - double-buffering?
        scd.Width = nScreenWidth;
        scd.Height = nScreenHeight;
        scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;      //how swap chain is to be used
        scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;         // swapping the memory location of back and front buffer, once flipped, discard back buffer content
        scd.SampleDesc.Count = 1;
        scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;     // allow full-screen transition

        ComPtr<IDXGISwapChain1> swapChain;
        // swap chain needs the queue so that it can force a flush on it
        ThrowIfFailed(factory->CreateSwapChainForHwnd(g_pCommandQueue.Get(), hWnd, &scd, nullptr, nullptr, &swapChain));

        ThrowIfFailed(swapChain.As(&g_pSwapChain));

        g_nFrameIndex = g_pSwapChain->GetCurrentBackBufferIndex();

        CreateDescriptorHeaps();
        CreateRenderTarget();
        InitPipeline();
        InitGraphics();
        InitConstants();
    }
}

void DiscardGraphicsResources()
{
    WaitForPreviousFrame();

    CloseHandle(g_hFenceEvent);
}

void PopulateCommandList()
{
    // command list allocators can only be reset when the associated command lists have finished execution on the GPU
    // apps should use fences to determine GPU execution progress
    ThrowIfFailed(g_pCommandAllocator->Reset());   

    // when ExecuteCommandList() is called on a particular command list, that command list can then be reset at any time and must be before re-recording
    ThrowIfFailed(g_pCommandList->Reset(g_pCommandAllocator.Get(), g_pPipelineState.Get()));

    // set necessary state
    g_pCommandList->SetGraphicsRootSignature(g_pRootSignature.Get());

    ID3D12DescriptorHeap *ppHeaps[] = {g_pCbvSrvHeap.Get(), g_pSamplerHeap.Get()};
    g_pCommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    g_pCommandList->SetGraphicsRootDescriptorTable(0, g_pCbvSrvHeap->GetGPUDescriptorHandleForHeapStart());
    g_pCommandList->SetGraphicsRootDescriptorTable(1, g_pSamplerHeap->GetGPUDescriptorHandleForHeapStart());

    g_pCommandList->RSSetViewports(1, &g_ViewPort);
    g_pCommandList->RSSetScissorRects(1, &g_ScissorRect);

    g_pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_pCommandList->IASetVertexBuffers(0, 1, &g_VertexBufferView);
    g_pCommandList->IASetIndexBuffer(&g_IndexBufferView);

    // indicate that the back buffer will be used as a render target. 
    g_pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                g_pRenderTargets[g_nFrameIndex].Get(),
                D3D12_RESOURCE_STATE_PRESENT,
                D3D12_RESOURCE_STATE_RENDER_TARGET));
    
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(g_pRtvHeap->GetCPUDescriptorHandleForHeapStart(), g_nFrameIndex, g_nRtvDescriptorSize);
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(g_pDsvHeap->GetCPUDescriptorHandleForHeapStart());
    g_pCommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    // 1 SRV + how many CBVs we have
    uint32_t nFrameResourceDescriptorOffset = 1 + g_nFrameIndex;
    CD3DX12_GPU_DESCRIPTOR_HANDLE cbvSrvHandle(g_pCbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), nFrameResourceDescriptorOffset, g_nCbvSrvDescriptorSize);

    g_pCommandList->SetGraphicsRootDescriptorTable(2, cbvSrvHandle);

    // clear the back buffer to a deep blue
    const FLOAT clearColor[] = {0.0f, 0.1f, 0.2f, 1.0f};
    g_pCommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    g_pCommandList->ClearDepthStencilView(g_pDsvHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // do 3D rendering on the back buffer here 
    {
        // draw the vertex buffer to the back buffer
        g_pCommandList->DrawIndexedInstanced(torus.m_indexCount, 1, 0, 0, 0);
    }

    // indicate that the back buffer will now be used to present
    g_pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                g_pRenderTargets[g_nFrameIndex].Get(),
                D3D12_RESOURCE_STATE_RENDER_TARGET,
                D3D12_RESOURCE_STATE_PRESENT));

    ThrowIfFailed(g_pCommandList->Close());
}

// update the contants
void Update()
{
    const float rotationSpeed = 0;// XM_PI * 2.0 / 360;
    static float rotationAngle = 0.0f;

    rotationAngle += rotationSpeed;
    if (rotationAngle >= XM_PI * 2.0)
        rotationAngle -= XM_PI * 2.0;

    const XMMATRIX m = XMMatrixRotationRollPitchYaw(rotationAngle, rotationAngle, 0.0f);
    XMStoreFloat4x4(&g_ConstantBufferData.m_modelMatrix, XMMatrixTranspose(m));
    XMStoreFloat4x4(&g_ConstantBufferData.m_viewMatrix, XMMatrixTranspose(g_mViewMatrix));
    XMStoreFloat4x4(&g_ConstantBufferData.m_projectionMatrix, XMMatrixTranspose(g_mProjectionMatrix));

    XMVECTOR v = XMVectorSet(3.0f, 3.0f, 3.0f, 1.0f);
    XMStoreFloat4(&g_ConstantBufferData.m_lightPosition, v);
    g_ConstantBufferData.m_lightColor = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    g_ConstantBufferData.m_ambientColor = XMFLOAT4(0.0f, 0.0f, 0.7f, 1.0f);
    g_ConstantBufferData.m_lightAttenuation = XMFLOAT4(1.0f, 0.0f, 0.0f, 0.0f);

    memcpy(g_pCbvDataBegin, &g_ConstantBufferData, sizeof(g_ConstantBufferData));
}

// render a single frame
void RenderFrame()
{
    // record all the commands we need to render the scene into the command list
    PopulateCommandList();

    // execute the command list
    ID3D12CommandList * ppCommandLists[] = { g_pCommandList.Get() };
    g_pCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // swap the back buffer and the front buffer
    ThrowIfFailed(g_pSwapChain->Present(1, 0));

    WaitForPreviousFrame();
}

// the WindowProc function prototype
LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstances, LPTSTR lpCmdLine, int nCmdShow)
{
    //the handle for the window
    HWND hWnd;
    //holds informatins for the window class
    WNDCLASSEX wc;

    //clear out the window class for use
    ZeroMemory(&wc, sizeof(WNDCLASSEX));

    //fill in the struct
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)COLOR_WINDOW;
    wc.lpszClassName = _T("WindowClass1");

    //register the window class
    RegisterClassEx(&wc);

    //create the window and use the result as the handle
    hWnd = CreateWindowEx(0,
        _T("WindowClass1"), //name of the window class
        _T("Hello, Engine![Direct 3D]"), //title of the window
        WS_OVERLAPPEDWINDOW, //window style
        100, //x-position of the window
        100, //y-position of the window
        nScreenWidth, //width of the window
        nScreenHeight, //height of the window
        NULL, //we have no parent window, nullptr
        NULL, //we aren't using menus, nullptr
        hInstance, //application handle
        NULL); //used with multiple windows, nullptr

    //display the window on the screen
    ShowWindow(hWnd, nCmdShow);

    //enter the main loop

    //holds windows event messages
    MSG msg;

    //wait for the next message in the queue, store the result in 'msg'
    while(GetMessage(&msg, nullptr, 0, 0))
    {
        //translate keystroke messages into the right format
        TranslateMessage(&msg);

        //send the message to the WindowProc function
        DispatchMessage(&msg);
    }

   //return this part of the WM_QUIT message to Windows
    return msg.wParam;
}

//the main message handler for the program
LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    LRESULT result = 0;
    bool wasHandled = false;

    //sort through and find what code to run for the message given
    switch(message)
    {
        case WM_CREATE:
            wasHandled = true;
            break;

        case WM_PAINT:
            CreateGraphicsResources(hWnd);
            Update();
            RenderFrame();
            wasHandled = true;
            break;
        
        // 屏幕拉伸会崩溃的原因
        // case WM_SIZE:
        //     if (g_pSwapChain)
        //     {
        //         DiscardGraphicsResources();
        //         g_pSwapChain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
        //     }
        //     wasHandled = true;
        //     break;

        case WM_DESTROY:
            DiscardGraphicsResources();
            PostQuitMessage(0);
            wasHandled = true;
            break;

        case WM_DISPLAYCHANGE:
            InvalidateRect(hWnd, nullptr, false);
            wasHandled = true;
            break;
    }

    //handle any messages the switch statement didn't
    if (!wasHandled)
    {
        result = DefWindowProc(hWnd, message, wParam, lParam);
    }

    return result;
}

/*
1. error LNK2001: unresolved external symbol _IID_ID3D12Device
增加dxguid.lib, https://github.com/microsoft/DirectX-Graphics-Samples/issues/567

*/



/*
1. descriptor heaps:
    in order to render with resources like textures, DX12 uses a descriptor to provide the meta-data required.
this helper provides a simple manager for the GPU memory heap of resource descriptors.

    D3D12_DESCRIPTOR_HEAP_TYPE_RTV : a heap of render target views
    D3D12_DESCRIPTOR_HEAP_TYPE_DSV : a heap of depth/stencil views
    D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER : a heap of (non-static) samplers
    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV : a heap of constant buffer views, shader resource views, and/or unordered-access views.

    https://github.com/microsoft/DirectXTK12/wiki/DescriptorHeap
    https://cocoa-programing.hatenablog.com/entry/2018/11/21/%E3%80%90DirectX12%E3%80%91%E3%83%87%E3%82%B9%E3%82%AF%E3%83%AA%E3%83%97%E3%82%BF%E3%83%92%E3%83%BC%E3%83%97%E3%81%AE%E4%BD%9C%E6%88%90%E3%80%90%E5%88%9D%E6%9C%9F%E5%8C%96%E3%80%91

2. IID_PPV_ARGS 
    a macro that simplifies the process of querying interfaces from COM(Component Object Model) objects.
the macro helps convert a pointer to a GUID(interface identifier) into the correct interface pointer type for use in function calls. 
*/


/* Developer Command Prompt for VS 2022

cl /EHsc /Debug /Zi /D_DEBUG_SHADER helloengine_d3d12.cpp user32.lib d3d12.lib d3dcompiler.lib dxgi.lib dxguid.lib


devenv /debug helloengine_d3d12.exe

*/