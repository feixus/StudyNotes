#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#define D3DX12_NO_STATE_OBJECT_HELPERS
#include "Graphics/d3dx12.h"

#include <windows.h>
#include <wrl/client.h>
#include <d3dcompiler.h>
#include <DirectXColors.h>
#include <DirectXMath.h>

#include <iostream>
#include <vector>
#include <string>
#include <queue>
#include <array>
#include <mutex>
#include <map>
#include <fstream>
#include <algorithm>
#include <assert.h>
#include <filesystem>
#include <list>

#include "Core/Delegates.h"
#include "Core/GameTimer.h"
#include "Core/BitField.h"
#include "Math/MathTypes.h"
#include "Math/MathHelp.h"
#include "External/Imgui/imgui.h"
#include "Core/Console.h"
#include "Graphics/D3DUtils.h"
#include "Core/StringHash.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::SimpleMath;
using namespace DirectX::Colors;

template <size_t S>
struct EnumSize;

template <>
struct EnumSize<1>
{
	typedef int8_t type;
};

template <>
struct EnumSize<2>
{
	typedef int16_t type;
};

template <>
struct EnumSize<4>
{
	typedef int32_t type;
};

template <>
struct EnumSize<8>
{
	typedef int64_t type;
};

template <class T>
struct EnumFlagSize
{
	typedef typename EnumSize<sizeof(T)>::type type;
};

#define DECLARE_BITMASK_TYPE(ENUMTYPE) \
inline constexpr ENUMTYPE operator | (ENUMTYPE a, ENUMTYPE b) throw() { return ENUMTYPE(((EnumFlagSize<ENUMTYPE>::type)a) | ((EnumFlagSize<ENUMTYPE>::type)b)); } \
inline constexpr ENUMTYPE& operator |= (ENUMTYPE& a, ENUMTYPE b) throw() { return (ENUMTYPE&)(((EnumFlagSize<ENUMTYPE>::type&)a) |= ((EnumFlagSize<ENUMTYPE>::type)b)); } \
inline constexpr ENUMTYPE operator & (ENUMTYPE a, ENUMTYPE b) throw() { return ENUMTYPE(((EnumFlagSize<ENUMTYPE>::type)a) & ((EnumFlagSize<ENUMTYPE>::type)b)); } \
inline constexpr ENUMTYPE& operator &= (ENUMTYPE& a, ENUMTYPE b) throw() { return (ENUMTYPE&)(((EnumFlagSize<ENUMTYPE>::type&)a) &= ((EnumFlagSize<ENUMTYPE>::type)b)); } \
inline constexpr ENUMTYPE operator ~ (ENUMTYPE a) throw() { return ENUMTYPE(~((EnumFlagSize<ENUMTYPE>::type)a)); } \
inline constexpr ENUMTYPE operator ^ (ENUMTYPE a, ENUMTYPE b) throw() { return ENUMTYPE(((EnumFlagSize<ENUMTYPE>::type)a) ^ ((EnumFlagSize<ENUMTYPE>::type)b)); } \
inline constexpr ENUMTYPE& operator ^= (ENUMTYPE& a, ENUMTYPE b) throw() { return (ENUMTYPE&)(((EnumFlagSize<ENUMTYPE>::type&)a) ^= ((EnumFlagSize<ENUMTYPE>::type)b)); } \
inline constexpr bool Any(ENUMTYPE a, ENUMTYPE b) throw() { return (ENUMTYPE)(((EnumFlagSize<ENUMTYPE>::type)a) & ((EnumFlagSize<ENUMTYPE>::type)b)) == b;}