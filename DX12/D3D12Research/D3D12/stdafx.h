#pragma once

#ifndef D3D12_USE_RENDERPASSES
#define D3D12_USE_RENDERPASSES 1
#endif

#ifndef WITH_CONSOLE
#define WITH_CONSOLE 1
#endif

#define CONCAT_IMPL(x, y) x##y
#define MACRO_CONCAT(x, y) CONCAT_IMPL(x, y)

#define checkf(expression, msg, ...) if (expression) {} else Console::LogFormat(LogType::FatalError, msg, __VA_ARGS__)
#define check(expression) checkf(expression, "")
#define noEntry() checkf(false, "Should not have reached this point!")

#define validateOncef(expression, msg, ...) if (!(expression)) { \
	static bool hasExecuted = false; \
	if (!hasExecuted) \
	{ \
		Console::LogFormat(LogType::Warning, "Assertion failed: '"#expression "'. " msg, ##__VA_ARGS__); \
		hasExecuted = true; \
	} \
} \

#define validateOnce(expression) validateOncef(expression, "")

#define USE_PIX 1

#include "Core/MinWindows.h"
#include <wrl/client.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DXProgrammableCapture.h>
#include <DirectXColors.h>
#include <DirectXMath.h>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

// STL
#include <assert.h>
// Containers
#include <string>
#include <array>
#include <vector>
#include <queue>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <memory>
// IO
#include <sstream>
#include <fstream>
#include <iostream>
#include <filesystem>
// Misc
#include <algorithm>
#include <mutex>

#include "Math/MathTypes.h"
#include "Math/Math.h"
#include "Core/String.h"
#include "Core/Thread.h"
#include "Core/CommandLine.h"
#include "Core/Delegates.h"
#include "Core/Time.h"
#include "Core/BitField.h"
#include "Core/Console.h"
#include "Core/StringHash.h"
#include "Core/CoreTypes.h"
#include "Graphics/Core/D3DUtils.h"

#include "External/d3dx12/d3dx12.h"
#include "External/d3dx12/d3dx12_extra.h"
#include "External/imgui/imgui.h"
#include <External/Dxc/dxcapi.h>

#define USE_OPTICK 1
#define OPTICK_ENABLE_TRACING 1
#define OPTICK_ENABLE_GPU_D3D12 1
#define OPTICK_ENABLE_GPU_VULKAN 0
#include "optick.h"

template<typename... Args>
std::string Sprintf(const char* pFormat, Args... args)
{
	char buff[256];
	sprintf_s(buff, pFormat, args...);
	return buff;
}