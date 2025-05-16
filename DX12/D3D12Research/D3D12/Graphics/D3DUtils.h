#pragma once

#include <filesystem>
#include <fstream>
#include <vector>
#include <cstddef>
#include <stdexcept>
#include <string>

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

/**
 * @brief Reads a file into a byte vector
 * @param filePath Path to the file to read
 * @param mode File open mode (default: std::ios::ate for reading from end)
 * @return std::vector<std::byte> containing the file contents
 * @throws std::runtime_error if file cannot be opened or read
 */
static std::vector<std::byte> ReadFile(const std::filesystem::path& filePath, std::ios_base::openmode mode = std::ios::ate)
{
	if (filePath.empty())
	{
		throw std::runtime_error("File path is empty");
	}

	if (!std::filesystem::exists(filePath))
	{
		throw std::runtime_error("File does not exist: " + filePath.string());
	}

	std::ifstream file(filePath, mode | std::ios::binary);
	if (!file)
	{
		throw std::runtime_error("Failed to open file: " + filePath.string());
	}

	const auto size = static_cast<size_t>(file.tellg());
	if (size == 0)
	{
		return {};
	}

	std::vector<std::byte> buffer(size);
	file.seekg(0);
	
	if (!file.read(reinterpret_cast<char*>(buffer.data()), size))
	{
		throw std::runtime_error("Failed to read file: " + filePath.string());
	}

	file.close();
	return buffer;
}


