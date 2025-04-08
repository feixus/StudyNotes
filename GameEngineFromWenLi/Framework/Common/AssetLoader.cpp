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
    AssetFilePtr fp = OpenFile(filePath, MY_OPEN_TEXT);
    Buffer* pBuffer = nullptr;

    if (fp) {
        size_t length = GetSize(fp);

        pBuffer = new Buffer(length + 1);
        fread(pBuffer->m_pData, length, 1, static_cast<FILE*>(fp));
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
    fclose(static_cast<FILE*>(fp));
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
    size_t sz;

    if (!fp) {
        fprintf(stderr, "Error reading file\n");
        return 0;
    }

    sz = fread(buf.m_pData, buf.m_szSize, 1, static_cast<FILE*>(fp));

    #ifdef DEBUG
    fprintf(stderr, "Read file '%s', %d bytes\n", filePath, length);
    #endif

    return sz;
}

int32_t My::AssetLoader::Seek(AssetFilePtr fp, long offset, AssetSeekBase where)
{
    return fseek(static_cast<FILE*>(fp), offset, static_cast<int32_t>(where));
}

