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
    FILE* fp = nullptr;
    std::filesystem::path upPath;
    std::filesystem::path fullPath;

    for (int32_t i = 0; i < 10; i++) {
        auto src = m_strSeachPaths.begin();
        bool looping = true;

        while (looping) {
            fullPath.assign(upPath);

            if (src != m_strSeachPaths.end()) {
                fullPath /= *src;
                fullPath /= "Asset";
            }
            else {
                fullPath /= "Asset";
                looping = false;
            }

            fullPath /= name;

			switch (mode) {
			    case MY_OPEN_TEXT:
				    fp = fopen(fullPath.string().c_str(), "r");
                    break;
                case MY_OPEN_BINARY:
                    fp = fopen(fullPath.string().c_str(), "rb");
				    break;
			}

			if (fp) {
                return (AssetFilePtr)fp;
			}
        }

        upPath /= "../";
    }

    return nullptr;
}

My::Buffer My::AssetLoader::SyncOpenAndReadText(const char* filePath)
{
    AssetFilePtr fp = OpenFile(filePath, MY_OPEN_TEXT);
    Buffer buff;

    if (fp) {
        size_t length = GetSize(fp);

        uint8_t* data = new uint8_t[length + 1];
        length = fread(data, 1, length, static_cast<FILE*>(fp));
#ifdef DEBUG
        fprintf(stderr, "Read file '%s', %d bytes\n", filePath, length);
#endif
        
        data[length] = '\0';
        buff.SetData(data, length + 1);

        CloseFile(fp);
    } else {
        fprintf(stderr, "Error opening file %s\n", filePath);
    }

    return buff;
}

void My::AssetLoader::CloseFile(AssetFilePtr& fp)
{
    fclose((FILE*)fp);
    fp = nullptr;
}

size_t My::AssetLoader::GetSize(const AssetFilePtr& fp)
{
    FILE* _fp = static_cast<FILE*>(fp);

    long pos = ftell(_fp);
    fseek(_fp, 0, SEEK_END);
    size_t length = ftell(_fp);
    fseek(_fp, pos, SEEK_SET);

    return length;
}

size_t My::AssetLoader::SyncRead(const AssetFilePtr& fp, Buffer& buf)
{
    
    if (!fp) {
        fprintf(stderr, "Error reading file\n");
        return 0;
    }
    auto* fileStream = static_cast<std::ifstream*>(fp);
    fileStream->read(reinterpret_cast<char*>(buf.GetData()), buf.GetDataSize());
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

