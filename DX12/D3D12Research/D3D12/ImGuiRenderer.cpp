#include "stdafx.h"
#include "ImGuiRenderer.h"
#include "CommandContext.h"
#include "D3DUtils.h"
#include "External/imgui/imgui.h"

ImGuiRenderer::ImGuiRenderer(ID3D12Device* pDevice)
	: m_pDevice(pDevice)
{
#if defined(_DEBUG)
	uint32_t compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	uint32_t compileFlags = 0;
#endif

	compileFlags |= D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;
	
	std::vector<std::byte> data = ReadFile("Resources/imgui.hlsl");
	
	ComPtr<ID3D10Blob> pErrorBlob, pVertexShader, pPixelShader;
	D3DCompile2(data.data(), data.size(), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, 0, nullptr, 0, pVertexShader.GetAddressOf(), pErrorBlob.GetAddressOf());
	if (pErrorBlob != nullptr)
	{
		std::wstring errorMessage((char*)pErrorBlob->GetBufferPointer(), (char*)pErrorBlob->GetBufferPointer() + pErrorBlob->GetBufferSize());
		std::wcout << errorMessage << std::endl;
		return;
	}

	pErrorBlob.Reset();
	D3DCompile2(data.data(), data.size(), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, 0, nullptr, 0, &pPixelShader, &pErrorBlob);
	if (pErrorBlob != nullptr)
	{
		std::wstring errorMessage((char*)pErrorBlob->GetBufferPointer(), (char*)pErrorBlob->GetBufferPointer() + pErrorBlob->GetBufferSize());
		std::wcout << errorMessage << std::endl;
		return;
	}

	// input layout
	std::vector<D3D12_INPUT_ELEMENT_DESC> elementDesc;
	elementDesc.push_back(D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
	elementDesc.push_back(D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
	elementDesc.push_back(D3D12_INPUT_ELEMENT_DESC{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });

	// root signature
	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc{};
	
	CD3DX12_ROOT_PARAMETER1 rootParameters[1];
	rootParameters[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_VERTEX);

	D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

	D3D12_STATIC_SAMPLER_DESC samplerDesc = CD3DX12_STATIC_SAMPLER_DESC(0);
	rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, &samplerDesc, rootSignatureFlags);

	ComPtr<ID3DBlob> signature, error;
	HR(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
	HR(m_pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_pRootSignature)));

	// pipeline state
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psDesc = {};
	psDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	psDesc.NodeMask = 0;
	psDesc.pRootSignature = m_pRootSignature.Get();
	psDesc.InputLayout.NumElements = (uint32_t)elementDesc.size();
	psDesc.InputLayout.pInputElementDescs = elementDesc.data();
	psDesc.VS = CD3DX12_SHADER_BYTECODE(pVertexShader.Get());
	psDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psDesc.SampleDesc.Count = 1;
	psDesc.SampleDesc.Quality = 0;
	psDesc.SampleMask = UINT_MAX;
	psDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	psDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	psDesc.DepthStencilState.DepthEnable = true;
	psDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	psDesc.PS = CD3DX12_SHADER_BYTECODE(pPixelShader.Get());
	psDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psDesc.NumRenderTargets = 1;
	psDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	HR(m_pDevice->CreateGraphicsPipelineState(&psDesc, IID_PPV_ARGS(m_pPipelineState.GetAddressOf())));

	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->AddFontDefault();
	unsigned char* pPixels;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pPixels, &width, &height);
}

ImGuiRenderer::~ImGuiRenderer()
{
	ImGui::DestroyContext();
}

void ImGuiRenderer::NewFrame()
{
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2((float)m_WindowWidth, (float)m_WindowHeight);
	ImGui::NewFrame();
}

void ImGuiRenderer::Render(CommandContext& context)
{
	context.GetCommandList()->SetPipelineState(m_pPipelineState.Get());
	context.GetCommandList()->SetGraphicsRootSignature(m_pRootSignature.Get());

	// copy the new data to the buffers
	ImGui::Render();
	ImDrawData* pDrawData = ImGui::GetDrawData();
	if (pDrawData->CmdListsCount == 0)
	{
		return;
	}

	Matrix projectionMatrix = XMMatrixOrthographicOffCenterLH(0.0f, (float)m_WindowWidth, (float)m_WindowHeight, 0.0f, 0.0f, 1.0f);
	context.SetDynamicConstantBufferView(0, &projectionMatrix, sizeof(Matrix));
	context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context.SetViewport({ 0, 0, m_WindowWidth, m_WindowHeight }, 0, 1);
	
	int vertexOffset = 0;
	int indexOffset = 0;
	for (int n = 0; n < pDrawData->CmdListsCount; n++)
	{
		const ImDrawList* pCmdList = pDrawData->CmdLists[n];
		context.SetDynamicVertexBuffer(0, pCmdList->VtxBuffer.Size, sizeof(ImDrawVert), pCmdList->VtxBuffer.Data);
		context.SetDynamicIndexBuffer(pCmdList->IdxBuffer.Size, pCmdList->IdxBuffer.Data);

		for (int cmdIndex = 0; cmdIndex < pCmdList->CmdBuffer.Size; cmdIndex++)
		{
			const ImDrawCmd* pCmd = &pCmdList->CmdBuffer[cmdIndex];
			if (pCmd->UserCallback)
			{
				pCmd->UserCallback(pCmdList, pCmd);
			}
			else
			{
				context.DrawIndexed(pCmd->ElemCount, indexOffset, vertexOffset);
			}
			indexOffset += pCmd->ElemCount;
		}
		vertexOffset += pCmdList->VtxBuffer.Size;
	}
}
