#pragma once

#ifndef D3D12_USE_RENDERPASSES
#define D3D12_USE_RENDERPASSES 1
#endif

#ifndef WITH_CONSOLE
#define WITH_CONSOLE 1
#endif

#define check(expression) if(expression){} else Console::LogFormat(LogType::FatalError, #expression)
#define checkf(expression, msg, ...) if (expression) {} else Console::LogFormat(LogType::FatalError, msg, __VA_ARGS__)
#define noEntry() checkf(false, "Should not have reached this point!")

#define USE_PIX 1

#include <windows.h>
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

inline int ToMultibyte(const wchar_t* pStr, char* pOut, int len)
{
	return WideCharToMultiByte(CP_UTF8, 0, pStr, -1, pOut, len, nullptr, nullptr);
}

inline int ToWidechar(const char* pStr, wchar_t* pOut, int len)
{
	return MultiByteToWideChar(CP_UTF8, 0, pStr, -1, pOut, len);
}