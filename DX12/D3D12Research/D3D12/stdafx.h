#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#define D3DX12_NO_STATE_OBJECT_HELPERS
#include "d3dx12.h"

#include <DirectXMath.h>
#include <wrl/client.h>
#include <windows.h>
#include <d3dcompiler.h>
#include <DirectXColors.h>

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

#include "D3DUtils.h"
#include "DescriptorHandle.h"
#include "External/SimpleMath/SimpleMath.h"
#include "GameTimer.h"
#include "BitField.h"
#include "MathTypes.h"
#include "MathHelp.h"

using namespace std;
using Microsoft::WRL::ComPtr;

using namespace DirectX;
using namespace DirectX::SimpleMath;
using namespace DirectX::Colors;

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")