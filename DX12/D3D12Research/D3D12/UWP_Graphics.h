#pragma once
#include "Graphics.h"

class CommandAllocatorPool;
class CommandQueue;
class CommandQueueManager;

using namespace Windows::UI::Core;

class UWP_Graphics : public Graphics
{
public:
	UWP_Graphics(UINT width, UINT height, std::wstring name);
	~UWP_Graphics();

	virtual void Initialize() override;

protected:
	virtual void CreateSwapchain() override;

private:
	ComPtr<ICoreWindow> coreWindow;
};