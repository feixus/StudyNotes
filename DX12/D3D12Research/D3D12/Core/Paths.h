#pragma once

namespace Paths
{
  bool IsSlash(const char c);

  std::string GetFileName(const std::string& path);
  std::string GetFileNameWithoutExtension(const std::string& path);
  std::string GetFileExtension(const std::string& path);
  std::string GetDirectoryPath(const std::string& path);

  std::string Normalize(const std::string& path);
  void NormalizeInline(std::string& path);

  std::string ChangeExtension(const std::string& path, const std::string& newExtension);

  std::string MakeRelativePath(const std::string& basePath, const std::string& filePath);

  std::string Combine(const std::string& a, const std::string& b);
  void Combine(const std::vector<std::string>& elements, std::string& output);

  bool FileExists(const std::string& path);
  bool DirectoryExists(const std::string& path);

  std::string GameDir();
  std::string SavedDir();
  std::string ScreenshotDir();
  std::string LogsDir();
  std::string ProfilingDir();
  std::string PakFilesDir();
  std::string ResourcesDir();
  std::string ConfigDir();
  std::string ShaderCacheDir();
  std::string ShaderDir();

  std::string GameIniFile();
  std::string EngineIniFile();

  std::string WorkingDirectory();

  bool CreateDirectoryTree(const std::string& path);
};
