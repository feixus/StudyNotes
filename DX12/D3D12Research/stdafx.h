#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include <wrl.h>
#include <d3dcompiler.h>

#include <iostream>
#include <vector>
#include <string>

#define D3DX12_NO_STATE_OBJECT_HELPERS
#include "d3dx12.h"

using namespace std;
using Microsoft::WRL::ComPtr;
using namespace DirectX;

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")