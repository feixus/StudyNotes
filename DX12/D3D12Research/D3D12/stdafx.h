#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#define D3DX12_NO_STATE_OBJECT_HELPERS
#include "d3dx12.h"

#include <DirectXMath.h>
#include <wrl.h>
#include <d3dcompiler.h>
#include <DirectXColors.h>

#include <iostream>
#include <vector>
#include <string>
#include <queue>
#include <array>
#include <mutex>

#include "D3DUtils.h"


using namespace std;
using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::Colors;

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")