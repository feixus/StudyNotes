#pragma once

#include "filesystem"

#define HR(hr) \
LogHRESULT(hr)

static bool LogHRESULT(HRESULT hr)
{
	if (SUCCEEDED(hr)) return true;

	CHAR* errorMsg;
	if (FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		nullptr, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPSTR)&errorMsg, 0, nullptr) != 0)
	{
		std::cout << "Error: " << errorMsg << std::endl;
	}

	__debugbreak();
	return false;
}

static Microsoft::WRL::ComPtr<ID3D12Resource> CreateDefaultBuffer(
	ID3D12Device* device,
	ID3D12GraphicsCommandList* cmdList,
	const void* initData,
	uint64_t byteSize,
	Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer)
{
	Microsoft::WRL::ComPtr<ID3D12Resource> defaultBuffer;

	// default buffer resource 
	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	auto resource_buffer_desc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
	HR(device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&resource_buffer_desc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(defaultBuffer.GetAddressOf())));

	// intermediate upload heap for copy CPU memory data into default buffer 
	auto heapProps_upload = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	HR(device->CreateCommittedResource(
		&heapProps_upload,
		D3D12_HEAP_FLAG_NONE,
		&resource_buffer_desc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(uploadBuffer.GetAddressOf())));

	// describe the data 
	D3D12_SUBRESOURCE_DATA subResourceData = {};
	subResourceData.pData = initData;
	subResourceData.RowPitch = byteSize;
	subResourceData.SlicePitch = subResourceData.RowPitch;

	// schedule to copy the data to the default buffer resource. 
	// at a high level, the helper function updateSubresources will copy the CPU memory into the intermediate upload heap
	// then using ID3D12CommandList::CopySubresourceRegion, the intermediate upload heap data will be copied to the default buffer
	auto resource_barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		defaultBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON, 
		D3D12_RESOURCE_STATE_COPY_DEST);
	cmdList->ResourceBarrier(1, &resource_barrier);

	UpdateSubresources<1>(cmdList, defaultBuffer.Get(), uploadBuffer.Get(), 0, 0, 1, &subResourceData);

	resource_barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		defaultBuffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, 
		D3D12_RESOURCE_STATE_GENERIC_READ);
	cmdList->ResourceBarrier(1, &resource_barrier);

	// note: uploadBuffer has to to be kept alive after the above function calls
	// because the command list has not been executed yet that performs the actual copy.
	// the caller can release the uploadbuffer after it knows the copy has been executed.

	return defaultBuffer;
}

static std::vector<std::byte> ReadFile(const std::filesystem::path& filePath, std::ios_base::openmode mode = std::ios::ate)
{
	if (!std::filesystem::exists(filePath))
	{
		throw std::runtime_error("Files does not exist: " + filePath.string());
	}

	std::ifstream file(filePath, mode);
	if (!file.is_open())
	{
		throw std::runtime_error("Files does not exist: " + filePath.string());
	}

	const auto size = static_cast<size_t>(file.tellg());
	if (size == 0)
	{
		return {};
	}

	std::vector<std::byte> buffer(size);
	file.seekg(0);
	file.read(reinterpret_cast<char*>(buffer.data()), size);

	return buffer;
}


