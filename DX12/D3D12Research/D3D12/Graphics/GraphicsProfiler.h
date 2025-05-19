#pragma once
class Graphics;
class ReadbackBuffer;
class CommandContext;

// readback GPU timestamp
//	- first create a query heap of type TIMESTAMP
//  - then create a readback buffer with D3D12_HEAP_TYPE_READBACK
//  - then insert timestamp queries in command lists with EndQuery
//  - then resolve both query data to the readback buffer with ResolveQueryData
//  - execute and wait for GPU to finish
//  - Map the readback buffer and compute time difference
class GraphicsProfiler
{
public:
	GraphicsProfiler(Graphics* pGraphics);
	~GraphicsProfiler();

	void Begin(CommandContext& context);
	void End(CommandContext& context);
	void Readback(int frameIndex);

private:
	int GetOffsetForFrame(int frameIndex);

	constexpr static int HEAP_SIZE = 512;

	Graphics* m_pGraphics;
	double m_SecondsPerTick{0.f};
	int m_CurrentIndex{0};
	ComPtr<ID3D12QueryHeap> m_pQueryHeap;
	std::unique_ptr<ReadbackBuffer> m_pReadBackBuffer;
};