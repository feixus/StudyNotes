#pragma once
#include "GraphicsResource.h"

class CommandContext;
class Graphics;

class GraphicsBuffer : public GraphicsResource
{
public:
	void Create(ID3D12Device* pDevice, uint32_t size, bool cpuVisible = false);
	void SetData(CommandContext* pContext, void* pData, uint32_t dataSize, uint32_t offset = 0);

	uint32_t GetSize() const { return m_Size; }

protected:
	uint32_t m_Size{0};
};

class StructuredBuffer : public GraphicsBuffer
{
public:
	void Create(Graphics* pGraphics, uint32_t elementStride, uint32_t elementCount, bool cpuVisible = false);

	D3D12_CPU_DESCRIPTOR_HANDLE GetSRV() const { return m_Srv; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetUAV() const { return m_Uav; }

private:
	D3D12_CPU_DESCRIPTOR_HANDLE m_Srv{};
	D3D12_CPU_DESCRIPTOR_HANDLE m_Uav{};
};