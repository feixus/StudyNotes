#pragma once

#define VERIFY_HR(hr) D3D::LogHRESULT(hr, nullptr, #hr, __FILE__, __LINE__)
#define VERIFY_HR_EX(hr, device) D3D::LogHRESULT(hr, device, #hr, __FILE__, __LINE__)

#ifdef _DEBUG
#define D3D_SETNAME(obj, name) D3D::SetObjectName(obj, name)
#else
#define D3D_SETNAME(obj, name)
#endif

namespace D3D
{
	inline std::string ResourceStateToString(D3D12_RESOURCE_STATES state)
	{
		if (state == 0)
		{
			return "COMMON";
		}

		char out[1024];
		out[0] = '\0';
		char* pCurrent = out;
		int i = 0;
		auto addText = [&](const char* pText) {
			if (i++ > 0)
			{
				*pCurrent++ = '/';
			}
			size_t len = strlen(pText);
			memcpy(pCurrent, pText, len);
			pCurrent += len;
			*pCurrent = '\0';
		};

#define STATE_CASE(name) if ((state & D3D12_RESOURCE_STATE_##name) == D3D12_RESOURCE_STATE_##name) addText(#name)
		STATE_CASE(VERTEX_AND_CONSTANT_BUFFER);
		STATE_CASE(INDEX_BUFFER);
		STATE_CASE(RENDER_TARGET);
		STATE_CASE(UNORDERED_ACCESS);
		STATE_CASE(DEPTH_WRITE);
		STATE_CASE(DEPTH_READ);
		STATE_CASE(NON_PIXEL_SHADER_RESOURCE);
		STATE_CASE(PIXEL_SHADER_RESOURCE);
		STATE_CASE(STREAM_OUT);
		STATE_CASE(INDIRECT_ARGUMENT);
		STATE_CASE(COPY_DEST);
		STATE_CASE(COPY_SOURCE);
		STATE_CASE(RESOLVE_DEST);
		STATE_CASE(RESOLVE_SOURCE);
		STATE_CASE(RAYTRACING_ACCELERATION_STRUCTURE);
		STATE_CASE(SHADING_RATE_SOURCE);
		STATE_CASE(GENERIC_READ);
		STATE_CASE(VIDEO_DECODE_READ);
		STATE_CASE(VIDEO_DECODE_WRITE);
		STATE_CASE(VIDEO_PROCESS_READ);
		STATE_CASE(VIDEO_PROCESS_WRITE);
		STATE_CASE(VIDEO_ENCODE_READ);
		STATE_CASE(VIDEO_ENCODE_WRITE);

#undef STATE_CASE
		return out;
	}

	inline const char* CommandlistTypeToString(D3D12_COMMAND_LIST_TYPE type)
	{
#define STATE_CASE(name) case D3D12_COMMAND_LIST_TYPE_##name: return #name
		switch (type)
		{
			STATE_CASE(DIRECT);
			STATE_CASE(BUNDLE);
			STATE_CASE(COMPUTE);
			STATE_CASE(COPY);
			STATE_CASE(VIDEO_DECODE);
			STATE_CASE(VIDEO_PROCESS);
			STATE_CASE(VIDEO_ENCODE);
		default:
			return "UNKNOWN";
		}
#undef STATE_CASE
	}

	inline void BeginPixCapture()
	{
		ComPtr<IDXGraphicsAnalysis> pAnalysis;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(pAnalysis.GetAddressOf()))))
		{
			pAnalysis->BeginCapture();
		}
	}

	inline void EndPixCapture()
	{
		ComPtr<IDXGraphicsAnalysis> pAnalysis;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(pAnalysis.GetAddressOf()))))
		{
			pAnalysis->EndCapture();
		}
	}

	class PixCaptureScope
	{
	public:
		PixCaptureScope()
		{
			BeginPixCapture();
		}

		~PixCaptureScope()
		{
			EndPixCapture();
		}
	};

	// device removed extend data
	inline void DREDHandler(ID3D12Device* pDevice)
	{
		//D3D12_AUTO_BREADCRUMB_OP
		constexpr const TCHAR* OpNames[] =
		{
			TEXT("SetMarker"),
			TEXT("BeginEvent"),
			TEXT("EndEvent"),
			TEXT("DrawInstanced"),
			TEXT("DrawIndexedInstanced"),
			TEXT("ExecuteIndirect"),
			TEXT("Dispatch"),
			TEXT("CopyBufferRegion"),
			TEXT("CopyTextureRegion"),
			TEXT("CopyResource"),
			TEXT("CopyTiles"),
			TEXT("ResolveSubresource"),
			TEXT("ClearRenderTargetView"),
			TEXT("ClearUnorderedAccessView"),
			TEXT("ClearDepthStencilView"),
			TEXT("ResourceBarrier"),
			TEXT("ExecuteBundle"),
			TEXT("Present"),
			TEXT("ResolveQueryData"),
			TEXT("BeginSubmission"),
			TEXT("EndSubmission"),
			TEXT("DecodeFrame"),
			TEXT("ProcessFrames"),
			TEXT("AtomicCopyBufferUint"),
			TEXT("AtomicCopyBufferUint64"),
			TEXT("ResolveSubresourceRegion"),
			TEXT("WriteBufferImmediate"),
			TEXT("DecodeFrame1"),
			TEXT("SetProtectedResourceSession"),
			TEXT("DecodeFrame2"),
			TEXT("ProcessFrames1"),
			TEXT("BuildRaytracingAccelerationStructure"),
			TEXT("EmitRaytracingAccelerationStructurePostBuildInfo"),
			TEXT("CopyRaytracingAccelerationStructure"),
			TEXT("DispatchRays"),
			TEXT("InitializeMetaCommand"),
			TEXT("ExecuteMetaCommand"),
			TEXT("EstimateMotion"),
			TEXT("ResolveMotionVectorHeap"),
			TEXT("SetPipelineState1"),
			TEXT("InitializeExtensionCommand"),
			TEXT("ExecuteExtensionCommand"),
			TEXT("DispatchMesh"),
			TEXT("EncodeFrame"),
			TEXT("ResolveEncoderOutputMetadata")
		};

		// D3D12_DRED_ALLOCATION_TYPE
		constexpr const TCHAR* AllocTypeNames[] =
		{
			TEXT("CommandQueue"),
			TEXT("CommandAllocator"),
			TEXT("PipelineState"),
			TEXT("CommandList"),
			TEXT("Fence"),
			TEXT("DescriptorHeap"),
			TEXT("Heap"),
			TEXT("QueryHeap"),
			TEXT("CommandSignature"),
			TEXT("PipelineLibrary"),
			TEXT("VideoDecoder"),
			TEXT("VideoProcessor"),
			TEXT("Resource"),
			TEXT("Pass"),
			TEXT("CryptoSession"),
			TEXT("CryptoSessionPolicy"),
			TEXT("ProtectedResourceSession"),
			TEXT("VideoDecoderHeap"),
			TEXT("CommandPool"),
			TEXT("CommandRecorder"),
			TEXT("StateObject"),
			TEXT("MetaCommand"),
			TEXT("SchedulingGroup"),
			TEXT("VideoMotionEstimator"),
			TEXT("VideoMotionVectorHeap"),
			TEXT("VideoExtensionCommand"),
			TEXT("VideoEncoder"),
			TEXT("VideoEncoderHeap")
		};

		ID3D12DeviceRemovedExtendedData1* pDred = nullptr;
		if (SUCCEEDED(pDevice->QueryInterface(IID_PPV_ARGS(&pDred))))
		{
			D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT dredAutoBreadcrumbsOutput;
			if (SUCCEEDED(pDred->GetAutoBreadcrumbsOutput(&dredAutoBreadcrumbsOutput)))
			{
				E_LOG(Warning, "[DRED] last tracked GPU operation:");

				const D3D12_AUTO_BREADCRUMB_NODE* node = dredAutoBreadcrumbsOutput.pHeadAutoBreadcrumbNode;
				while (node)
				{
					int32_t lastCompletedOp = *node->pLastBreadcrumbValue;

					E_LOG(Warning, "[DRED] Commandlist \"%s\" on CommandQueue \"%s\", %d completed of %d", node->pCommandListDebugNameW, node->pCommandQueueDebugNameW, lastCompletedOp, node->BreadcrumbCount);
					
					int32_t firstOp = Math::Max(lastCompletedOp - 5, 0);
					int32_t lastOp = Math::Min(lastCompletedOp + 5, int32_t(node->BreadcrumbCount) - 1);

					for (int32_t op = firstOp; op <= lastOp; op++)
					{
						D3D12_AUTO_BREADCRUMB_OP breadcrumbOp = node->pCommandHistory[op];
						const TCHAR* opName = (breadcrumbOp < std::size(OpNames)) ? OpNames[breadcrumbOp] : TEXT("Unknown Op");
						E_LOG(Warning, "\tOp: %d, %s%s", op, opName, (op + 1 == lastCompletedOp) ? TEXT(" - Last completed") : TEXT(""));
					}
					node = node->pNext;
				}
			}

			D3D12_DRED_PAGE_FAULT_OUTPUT dredPageFaultOutput;
			if (SUCCEEDED(pDred->GetPageFaultAllocationOutput(&dredPageFaultOutput)) && dredPageFaultOutput.PageFaultVA != 0)
			{
				E_LOG(Warning, "[DRED] PageFault at VA GPUAddress \"0x%x\"", dredPageFaultOutput.PageFaultVA);

				const D3D12_DRED_ALLOCATION_NODE* node = dredPageFaultOutput.pHeadExistingAllocationNode;
				if (node)
				{
					E_LOG(Warning, "[DRED] active objects with VS ranges that match the faulting VA:");
					while(node)
					{
						int32_t alloc_type_index = node->AllocationType - D3D12_DRED_ALLOCATION_TYPE_COMMAND_QUEUE;
						const TCHAR* allocTypeName = (alloc_type_index < std::size(AllocTypeNames)) ? AllocTypeNames[alloc_type_index] : TEXT("Unknown Alloc");
						E_LOG(Warning, "\tName: %s (Type: %s)", node->ObjectNameW, allocTypeName);
						node = node->pNext;
					}
				}

				node = dredPageFaultOutput.pHeadRecentFreedAllocationNode;
				if (node)
				{
					E_LOG(Warning, "[DRED] recent freed objects with VA ranges that match the faulting VA:");
					while(node)
					{
						int32_t alloc_type_index = node->AllocationType - D3D12_DRED_ALLOCATION_TYPE_COMMAND_QUEUE;
						const TCHAR* allocTypeName = (alloc_type_index < std::size(AllocTypeNames)) ? AllocTypeNames[alloc_type_index] : TEXT("Unknown Alloc");
						E_LOG(Warning, "\tName: %s (Type: %s)", node->ObjectNameW, allocTypeName);
						node = node->pNext;
					}
				}
			}
		}
	}

	inline std::string GetErrorString(HRESULT errorCode, ID3D12Device* pDevice)
	{
		std::stringstream str;
		char* errorMsg;
		if (FormatMessageA(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
			nullptr, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPSTR)&errorMsg, 0, nullptr) != 0)
		{
			str << errorMsg;
			LocalFree(errorMsg);
		}
		if (errorCode == DXGI_ERROR_DEVICE_REMOVED && pDevice)
		{
			ComPtr<ID3D12InfoQueue> pInfo;
			pDevice->QueryInterface(IID_PPV_ARGS(pInfo.GetAddressOf()));
			if (pInfo)
			{
				str << "Validation Layer: \n";
				for (uint64_t i = 0; i < pInfo->GetNumStoredMessages(); ++i)
				{
					size_t messageLength = 0;
					pInfo->GetMessage(0, nullptr, &messageLength);
					D3D12_MESSAGE* pMessage = (D3D12_MESSAGE*)malloc(messageLength);
					pInfo->GetMessage(0, pMessage, &messageLength);
					str << pMessage->pDescription << "\n";
					free(pMessage);
				}
			}

			HRESULT removedReason = pDevice->GetDeviceRemovedReason();
			str << " \n- Device Removed Reason: " << GetErrorString(removedReason, nullptr);
			
			DREDHandler(pDevice);
		}
		return str.str();
	}

	inline bool LogHRESULT(HRESULT hr, ID3D12Device* pDevice, const char* pCode, const char* pFileName, uint32_t lineNumber)
	{
		if (SUCCEEDED(hr))
		{
			return true;
		}

		E_LOG(Error, "%s:%d: %s - %s", pFileName, lineNumber, GetErrorString(hr, pDevice).c_str(), pCode);
		__debugbreak();

		return false;
	}

	inline void SetObjectName(ID3D12Object* pObject, const char* pName)
	{
		if (pObject)
		{
			VERIFY_HR(pObject->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)strlen(pName), pName));
		}
	}

	inline std::string GetObjectName(ID3D12Object* pObject)
	{
		std::string name;
		if (pObject)
		{
			UINT size = 0;
			VERIFY_HR(pObject->GetPrivateData(WKPDID_D3DDebugObjectName, &size, nullptr));
			name.resize(size);
			VERIFY_HR(pObject->GetPrivateData(WKPDID_D3DDebugObjectName, &size, &name[0]));
		}
		return name;
	}

	inline bool IsBlockCompressFormat(DXGI_FORMAT format)
	{
		switch (format)
		{
		case DXGI_FORMAT_BC1_TYPELESS:
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC1_UNORM_SRGB:
		case DXGI_FORMAT_BC2_TYPELESS:
		case DXGI_FORMAT_BC2_UNORM:
		case DXGI_FORMAT_BC2_UNORM_SRGB:
		case DXGI_FORMAT_BC3_TYPELESS:
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC3_UNORM_SRGB:
		case DXGI_FORMAT_BC4_TYPELESS:
		case DXGI_FORMAT_BC4_UNORM:
		case DXGI_FORMAT_BC4_SNORM:
		case DXGI_FORMAT_BC5_TYPELESS:
		case DXGI_FORMAT_BC5_UNORM:
		case DXGI_FORMAT_BC5_SNORM:
		case DXGI_FORMAT_BC6H_TYPELESS:
		case DXGI_FORMAT_BC6H_UF16:
		case DXGI_FORMAT_BC6H_SF16:
		case DXGI_FORMAT_BC7_TYPELESS:
		case DXGI_FORMAT_BC7_UNORM:
		case DXGI_FORMAT_BC7_UNORM_SRGB:
			return true;
		default:
			return false;
		}
	}

	inline DXGI_FORMAT GetSrvFormatFromDepth(DXGI_FORMAT format)
	{
		switch (format)
		{
			// 32-bit Z w/ Stencil
		case DXGI_FORMAT_R32G8X24_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
		case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
			return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;

			// No Stencil
		case DXGI_FORMAT_R32_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_R32_FLOAT:
			return DXGI_FORMAT_R32_FLOAT;

			// 24-bit Z
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
			return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;

			// 16-bit Z w/o Stencil
		case DXGI_FORMAT_R16_TYPELESS:
		case DXGI_FORMAT_D16_UNORM:
		case DXGI_FORMAT_R16_UNORM:
			return DXGI_FORMAT_R16_UNORM;

		default:
			return DXGI_FORMAT_UNKNOWN;
		}
	}

	inline DXGI_FORMAT GetDsvFormat(DXGI_FORMAT format)
	{
		switch (format)
		{
		case DXGI_FORMAT_R32_TYPELESS:
		case DXGI_FORMAT_R32_FLOAT:
			return DXGI_FORMAT_D32_FLOAT;
		case DXGI_FORMAT_R16_TYPELESS:
		case DXGI_FORMAT_R16_FLOAT:
			return DXGI_FORMAT_D16_UNORM;
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
			return DXGI_FORMAT_D24_UNORM_S8_UINT;
		case DXGI_FORMAT_R32G8X24_TYPELESS:
			return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
		}
		return format;
	}

	inline bool HasStencil(DXGI_FORMAT format)
	{
		return format == DXGI_FORMAT_D24_UNORM_S8_UINT || format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
	}

	inline int GetFormatRowDataSize(DXGI_FORMAT format, unsigned int width)
	{
		switch (format)
		{
		case DXGI_FORMAT_R8_UNORM:
		case DXGI_FORMAT_A8_UNORM:
		case DXGI_FORMAT_R8_UINT:
			return (unsigned)width;

		case DXGI_FORMAT_R8G8_UNORM:
		case DXGI_FORMAT_R16_UNORM:
		case DXGI_FORMAT_R16_FLOAT:
		case DXGI_FORMAT_R16_TYPELESS:
		case DXGI_FORMAT_R16_UINT:
			return (unsigned)(width * 2);

		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_R16G16_UNORM:
		case DXGI_FORMAT_R16G16_FLOAT:
		case DXGI_FORMAT_R32_FLOAT:
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_R32_TYPELESS:
		case DXGI_FORMAT_R32_UINT:
			return (unsigned)(width * 4);

		case DXGI_FORMAT_R16G16B16A16_UNORM:
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
			return (unsigned)(width * 8);

		case DXGI_FORMAT_R32G32B32A32_FLOAT:
			return (unsigned)(width * 16);

		case DXGI_FORMAT_BC1_TYPELESS:
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC1_UNORM_SRGB:
		case DXGI_FORMAT_BC4_TYPELESS:
		case DXGI_FORMAT_BC4_UNORM:
		case DXGI_FORMAT_BC4_SNORM:
			return (unsigned)(((width + 3) >> 2) * 8);

		case DXGI_FORMAT_BC2_TYPELESS:
		case DXGI_FORMAT_BC2_UNORM:
		case DXGI_FORMAT_BC2_UNORM_SRGB:
		case DXGI_FORMAT_BC3_TYPELESS:
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC3_UNORM_SRGB:
		case DXGI_FORMAT_BC5_TYPELESS:
		case DXGI_FORMAT_BC5_UNORM:
		case DXGI_FORMAT_BC5_SNORM:
		case DXGI_FORMAT_BC6H_TYPELESS:
		case DXGI_FORMAT_BC6H_UF16:
		case DXGI_FORMAT_BC6H_SF16:
		case DXGI_FORMAT_BC7_TYPELESS:
		case DXGI_FORMAT_BC7_UNORM:
		case DXGI_FORMAT_BC7_UNORM_SRGB:
			return (unsigned)(((width + 3) >> 2) * 16);
		case DXGI_FORMAT_R32G32B32_FLOAT:
			return width * 3 * sizeof(float);
		case DXGI_FORMAT_R32G32_FLOAT:
			return width * 2 * sizeof(float);
		default:
			assert(false);
			return 0;
		}
	}
}


