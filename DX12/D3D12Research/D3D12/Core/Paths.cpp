#include "stdafx.h"
#include "Paths.h"

#include <filesystem>

namespace Paths
{
    bool IsSlash(const char c)
    {
        if (c == '/')
            return true;
        return c == '/';
    }

    std::string GetFileName(const std::string& path)
    {
        return std::filesystem::path(path).filename().string();
    }

    std::string GetFileNameWithoutExtension(const std::string& path)
    {
        return std::filesystem::path(path).filename().stem().string();
    }

    std::string GetFileExtension(const std::string& path)
    {
        return std::filesystem::path(path).extension().string();
    }

    std::string GetDirectoryPath(const std::string& path)
    {
	    std::filesystem::path dir = std::filesystem::path(path).parent_path();

	    std::string dir_str = dir.generic_string();
	    if (!dir_str.empty() && dir_str.back() != '/') {
		    dir_str += '/';
	    }
	    return dir_str;
    }

    std::string Normalize(const std::string& path)
    {
        return std::filesystem::weakly_canonical(path).string();
    }

    void NormalizeInline(std::string& path)
    {
        path = Normalize(path);
    }

    std::string ChangeExtension(const std::string& path, const std::string& newExtension)
    {
        return std::filesystem::path(path).replace_extension(newExtension).string();
    }

    std::string MakeRelativePath(const std::string& basePath, const std::string& filePath)
    {
        return std::filesystem::relative(filePath, basePath).string();
    }

    std::string Combine(const std::string& a, const std::string& b)
    {
        return (std::filesystem::path(a) / b).string();
    }

    void Combine(const std::vector<std::string>& elements, std::string& output)
    {
        std::stringstream stream;
        for (size_t i = 0; i < elements.size(); i++)
        {
            stream << elements[i];
            if (elements[i].back() != '/' && i != elements.size() - 1)
            {
                stream << '/';
            }
        }
        output = stream.str();
    }

    bool FileExists(const std::string& path)
    {
        return std::filesystem::exists(path);
    }

    bool DirectoryExists(const std::string& path)
    {
        return std::filesystem::exists(path) && std::filesystem::is_directory(path);
    }

    std::string GameDir()
    {
        return "./";
    }

    std::string SavedDir()
    {
        return GameDir() + "Saved/";
    }

    std::string ScreenshotDir()
    {
        return GameDir() + "Screenshot/";
    }

    std::string LogsDir()
    {
        return SavedDir() + "Logs/";
    }

    std::string ProfilingDir()
    {
        return SavedDir() + "Profiling/";
    }

    std::string PakFilesDir()
    {
        return GameDir() + "Paks/";
    }

    std::string ResourcesDir()
    {
        return GameDir() + "Resources/";
    }

    std::string ConfigDir()
    {
        return SavedDir() + "Config/";
    }

    std::string ShaderCacheDir()
    {
        return SavedDir() + "ShaderCache/";
    }

    std::string ShaderDir()
    {
        return ResourcesDir() + "Shaders/";
    }

    std::string GameIniFile()
    {
        return ConfigDir() + "Game.ini";
    }

    std::string EngineIniFile()
    {
        return ConfigDir() + "Engine.ini";
    }

    std::string WorkingDirectory()
    {
        char path[256];
        GetModuleFileName(nullptr, path, sizeof(path));
        return path;
    }

    bool CreateDirectoryTree(const std::string& path)
    {
        return std::filesystem::create_directories(path);
    }
}
