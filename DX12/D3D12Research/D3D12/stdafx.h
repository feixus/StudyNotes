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

#include "Core/GameTimer.h"
#include "Core/BitField.h"
#include "Math/MathTypes.h"
#include "Math/MathHelp.h"
#include "External/Imgui/imgui.h"
#include "Core/Console.h"
#include "Graphics/D3DUtils.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::SimpleMath;
using namespace DirectX::Colors;