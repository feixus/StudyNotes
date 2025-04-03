#include <stddef.h>
#include "MemoryManager.hpp"

namespace My
{
    extern MemoryManager* g_pMemoryManager;

    class Buffer
    {
    public:
        Buffer() : m_pData(nullptr), m_szSize(0), m_szAlignment(alignof(uint32_t)) {}
        Buffer(size_t size, size_t alignment = 4) : m_pData(nullptr), m_szAlignment(alignment) { m_pData = reinterpret_cast<uint8_t*>(g_pMemoryManager->Allocate(size, alignment)); }
        ~Buffer() { if (m_pData) g_pMemoryManager->Free(m_pData, m_szSize); m_pData = nullptr; }
    private:
        uint8_t* m_pData;
        size_t m_szSize;
        size_t m_szAlignment;
    };
}