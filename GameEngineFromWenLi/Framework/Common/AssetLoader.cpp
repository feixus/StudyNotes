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
    std::vector<std::string>::iterator src = m_strSeachPaths.begin();

    while (src != m_strSeachPaths.end()) {
        if (!(*src).compare(path)) {
            return true;
        }
        src++;
    }

    m_strSeachPaths.push_back(path);
    return true;
}

bool My::AssetLoader::RemoveSearchPath(const char* path)
{
    std::vector<std::string>::iterator src = m_strSeachPaths.begin();

    while (src != m_strSeachPaths.end()) {
        if (!(*src).compare(path)) {
            m_strSeachPaths.erase(src);
            return true;
        }
        src++;
    }

    return true;
}

bool My::AssetLoader::FileExists(const char* filePath)
{
    AssetFilePtr fp = OpenFile(filePath, MY_OPEN_BINARY);
    if (fp) {
        CloseFile(fp);
        return true;
    }
    return false;
}

My::AssetLoader::AssetFilePtr My::AssetLoader::OpenFile(const char* name, AssetOpenMode mode)
{
    FILE* fp = nullptr;
    std::string upPath;
    std::string fullPath;
    for (int32_t i = 0; i < 10; i++) {
        std::vector<std::string>::iterator src = m_strSeachPaths.begin();
        bool looping = true;
        while (looping) {
            fullPath.assign(upPath);
            if (src != m_strSeachPaths.end()) {
                fullPath.append(*src);
                fullPath.append("/Asset/");
                src++;
            }
            else {
                fullPath.append("Asset/");
                looping = false;
            }
            fullPath.append(name);

            #ifdef DEBUG
            fprintf(stderr, "Trying to open %s\n", fullPath.c_str());
            #endif

            switch(mode) {
                case MY_OPEN_TEXT:
                    fp = fopen(fullPath.c_str(), "r");
                    break;
                case MY_OPEN_BINARY:
                    fp = fopen(fullPath.c_str(), "rb");
                    break;
            }

            if (fp)
                return (AssetFilePtr)fp;
        }

        upPath.append("../");
    }

    return nullptr;
}

My::Buffer My::AssetLoader::SyncOpenAndReadText(const char* filePath)
{
    Buffer* pBuffer = nullptr;

    AssetFilePtr fp = OpenFile(filePath, MY_OPEN_TEXT);
    if (fp) {
       
        
    }

    return *pBuffer;
}