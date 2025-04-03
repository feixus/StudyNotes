#pragma once
#include <cstdio>
#include <utility>
#include <vector>
#include <string>

#include "IRuntimeModule.hpp"
#include "Buffer.hpp"


namespace My
{
    class AssetLoader : public IRuntimeModule
    {
    public:
        virtual ~AssetLoader() = default;

        virtual int Initialize();
        virtual void Finalize();

        virtual void Tick();

        using AssetFilePtr = void*;

        enum AssetOpenMode {
            MY_OPEN_TEXT = 0,
            MY_OPEN_BINARY = 1,
        };

        enum AssetSeekBase {
            MY_SEEK_SET = 0,
            MY_SEEK_CUR = 1,
            MY_SEEK_END = 2,
        };

        bool AddSearchPath(const char* path);
        bool RemoveSearchPath(const char* path);

        bool FileExists(const char* path);

        AssetFilePtr OpenFile(const char* name, AssetOpenMode mode);
        void CloseFile(const AssetFilePtr& fp);

        Buffer SyncOpenAndReadText(const char* filePath);
        size_t SyncRead(const AssetFilePtr& fp, Buffer& buf);

        size_t GetSize(const AssetFilePtr& fp);

        int32_t Seek(const AssetFilePtr& fp, long offset, AssetSeekBase where);

        inline std::string SyncOpenAndReadTextFileToString(const char* fileName)
        {
            std::string result;
            Buffer buffer = SyncOpenAndReadText(fileName);
            char* content = reinterpret_cast<char*>(buffer.m_pData);

            if (content) {
                result = std::string(std::move(content));
            }

            return result;
        }

    private:
        std::vector<std::string> m_strSeachPaths;
    };
}