#pragma once
class CommandContext;
class Graphics;

class ImGuiRenderer
{
public:
	ImGuiRenderer(Graphics* pDevice);
	~ImGuiRenderer();

	void NewFrame();
	void Render(CommandContext& context);

private:
	static const uint32_t m_WindowWidth{ 1240 };
	static const uint32_t m_WindowHeight{ 720 };

	void LoadShaders(const char* pFilePath, ComPtr<ID3DBlob>* pVertexShaderCode, ComPtr<ID3DBlob>* pPixelShaderCode);
	void CreateRootSignature();
	void CreatePipelineState(const ComPtr<ID3DBlob>& pVertexShaderCode, const ComPtr<ID3DBlob>& pPixelShaderCode);
	void InitializeImGui();

	Graphics* m_pGraphics;
	ComPtr<ID3D12PipelineState> m_pPipelineState;
	ComPtr<ID3D12RootSignature> m_pRootSignature;
};
