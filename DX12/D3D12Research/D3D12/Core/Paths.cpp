#include "stdafx.h"
#include "Paths.h"

#include <filesystem>

bool Paths::IsSlash(const char c)
{
    if (c == '\\')
        return true;
    return c == '/';
}

std::string Paths::GetFileName(const std::string& path)
{
    return std::filesystem::path(path).filename().string();
}

std::string Paths::GetFileNameWithoutExtension(const std::string& path)
{
    return std::filesystem::path(path).filename().stem().string();
}

std::string Paths::GetFileExtension(const std::string& path)
{
    return std::filesystem::path(path).extension().string();
}

std::string Paths::GetDirectoryPath(const std::string& path)
{
	std::filesystem::path dir = std::filesystem::path(path).parent_path();

	std::string dir_str = dir.generic_string();
	if (!dir_str.empty() && dir_str.back() != '/') {
		dir_str += '/';
	}
	return dir_str;
}

std::string Paths::Normalize(const std::string& path)
{
    return std::filesystem::weakly_canonical(path).string();
}

void Paths::NormalizeInline(std::string& path)
{
    path = Normalize(path);
}

std::string Paths::ChangeExtension(const std::string& path, const std::string& newExtension)
{
    return std::filesystem::path(path).replace_extension(newExtension).string();
}

std::string Paths::MakeRelativePath(const std::string& basePath, const std::string& filePath)
{
    return std::filesystem::relative(filePath, basePath).string();
}

std::string Paths::Combine(const std::string& a, const std::string& b)
{
    return (std::filesystem::path(a) / b).string();
}

void Paths::Combine(const std::vector<std::string>& elements, std::string& output)
{
    output = "";
    for (const auto& element : elements)
    {
        if (!output.empty())
            output += "/";
        output += element;
    }
}

bool Paths::FileExists(const std::string& path)
{
    return std::filesystem::exists(path);
}

bool Paths::DirectoryExists(const std::string& path)
{
    return std::filesystem::exists(path) && std::filesystem::is_directory(path);
}

std::string Paths::GameDir()
{
    return ".\\";
}

std::string Paths::SavedDir()
{
    return GameDir() + "Save\\";
}

std::string Paths::ScreenshotDir()
{
    return GameDir() + "Screenshot\\";
}

std::string Paths::LogsDir()
{
    return SavedDir() + "Logs\\";
}

std::string Paths::ProfilingDir()
{
    return SavedDir() + "Profiling\\";
}

std::string Paths::PakFilesDir()
{
    return GameDir() + "Paks\\";
}

std::string Paths::ResourcesDir()
{
    return GameDir() + "Resources\\";
}

std::string Paths::ConfigDir()
{
    return SavedDir() + "Config\\";
}

std::string Paths::ShaderCacheDir()
{
    return SavedDir() + "ShaderCache\\";
}

std::string Paths::GameIniFile()
{
    return ConfigDir() + "Game.ini";
}

std::string Paths::EngineIniFile()
{
    return ConfigDir() + "Engine.ini";
}

std::string Paths::WorkingDirectory()
{
    char path[256];
    GetModuleFileName(nullptr, path, sizeof(path));
    return path;
}

bool Paths::CreateDirectoryTree(const std::string& path)
{
    return std::filesystem::create_directories(path);
}
