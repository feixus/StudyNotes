#include <fstream>
#include <filesystem>
#include <string>

#include "AssetLoader.hpp"

int My::AssetLoader::Initialize()
{
    return 0;
}

void My::AssetLoader::Finalize()
{
    m_strSeachPaths.clear();
}

void My::AssetLoader::Tick()
{

}

bool My::AssetLoader::AddSearchPath(const char* path)
{
    for (const auto& src : m_strSeachPaths) {
        if (src == path) {
            return true;
        }
    }

    m_strSeachPaths.push_back(path);
    return true;
}

bool My::AssetLoader::RemoveSearchPath(const char* path)
{
    auto it = std::remove(m_strSeachPaths.begin(), m_strSeachPaths.end(), path);
    if (it != m_strSeachPaths.end()) {
        m_strSeachPaths.erase(it, m_strSeachPaths.end());
        return true;
    }

    return false;
}

bool My::AssetLoader::FileExists(const char* filePath)
{
    return std::filesystem::exists(filePath);
}

My::AssetLoader::AssetFilePtr My::AssetLoader::OpenFile(const char* name, AssetOpenMode mode)
{
    std::filesystem::path upPath;
    std::filesystem::path assetPath;

    for (int32_t i = 0; i < 10; i++) {
        std::vector<std::string>::iterator src = m_strSeachPaths.begin();
        bool looping = true;

        while (looping) {
            assetPath = upPath;

            if (src != m_strSeachPaths.end()) {
                assetPath /= *src;
                assetPath /= "Asset";
            } else {
                assetPath /= "Asset";
                looping = false;
            }

            std::filesystem::path fullPath = assetPath / name;

            #ifdef DEBUG
            std::cerr << "Trying to open " << fullPath << std::endl;
            #endif

			if (std::filesystem::exists(fullPath)) {
				std::ios_base::openmode openMode = std::ios::in;
				switch (mode) {
				case MY_OPEN_TEXT:
					openMode = std::ios::in;
					break;
				case MY_OPEN_BINARY:
					openMode = std::ios::in | std::ios::binary;
					break;
				}

				std::fstream* fileStream = new std::fstream(fullPath, openMode);
				if (fileStream->is_open()) {
					return static_cast<AssetFilePtr>(fileStream);
				}
				else {
					delete fileStream;
				}
			}
        }

        upPath /= "../";
    }

    return nullptr;
}

My::Buffer My::AssetLoader::SyncOpenAndReadText(const char* filePath)
{
    AssetFilePtr fp = OpenFile(filePath, MY_OPEN_TEXT);
    Buffer* pBuffer = nullptr;

    if (fp) {
        size_t length = GetSize(fp);

        pBuffer = new Buffer(length + 1);
        auto* fileStream = static_cast<std::ifstream*>(fp);
        fileStream->read(reinterpret_cast<char*>(pBuffer->m_pData), length);
        pBuffer->m_pData[length] = '\0';

        CloseFile(fp);
    } else {
        fprintf(stderr, "Error opening file %s\n", filePath);
        pBuffer = new Buffer(0);
    }

#ifdef DEBUG
    fprintf(stderr, "Read file '%s', %d bytes\n", filePath, length);
#endif

    return *pBuffer;
}

void My::AssetLoader::CloseFile(AssetFilePtr& fp)
{
    auto* fileStream = static_cast<std::fstream*>(fp);
    fileStream->close();
    delete fileStream;
    fp = nullptr;
}

size_t My::AssetLoader::GetSize(const AssetFilePtr& fp)
{
    auto* fileStream = static_cast<std::ifstream*>(fp);
    auto currentPos = fileStream->tellg();
    fileStream->seekg(0, std::ios::end);
    size_t length = fileStream->tellg();
    fileStream->seekg(currentPos, std::ios::beg);

    return length;
}

size_t My::AssetLoader::SyncRead(const AssetFilePtr& fp, Buffer& buf)
{
    
    if (!fp) {
        fprintf(stderr, "Error reading file\n");
        return 0;
    }
    auto* fileStream = static_cast<std::ifstream*>(fp);
    fileStream->read(reinterpret_cast<char*>(buf.m_pData), buf.m_szSize);
    size_t sz = fileStream->gcount();
    
    #ifdef DEBUG
    fprintf(stderr, "Read file %d bytes\n", sz);
    #endif

    return sz;
}

int32_t My::AssetLoader::Seek(AssetFilePtr fp, long offset, AssetSeekBase where)
{
    if (!fp) return -1;

    auto* fileStream = static_cast<std::ifstream*>(fp);
    std::ios_base::seekdir dir;

    switch (where) {
    case MY_SEEK_SET:
        dir = std::ios::beg;
        break;
    case MY_SEEK_CUR:
        dir = std::ios::cur;
        break;
    case MY_SEEK_END:
        dir = std::ios::end;
        break;
    default:
        return -1;
    }

    fileStream->seekg(offset, dir);
    return fileStream->good() ? 0 : -1;
}

