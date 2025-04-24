#pragma once

class CommandContext;

class ImGuiRenderer
{
public:
	ImGuiRenderer(ID3D12Device* pDevice);
	~ImGuiRenderer();

	void NewFrame();
	void Render(CommandContext& context);

private:
	static const uint32_t m_WindowWidth{ 1240 };
	static const uint32_t m_WindowHeight{ 720 };

	ID3D12Device* m_pDevice;
	ComPtr<ID3D12PipelineState> m_pPipelineState;
	ComPtr<ID3D12RootSignature> m_pRootSignature;
};
