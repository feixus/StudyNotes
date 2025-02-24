#pragma once

#include "IRuntimeModule.hpp"
#include "Allocator.hpp"
#include <new>

namespace My 
{
    class MemoryManager : implements IRuntimeModule
    {
    public:
        template<typename T, typename... Arguments>
        T* New(Arguments... parameters)
        {
            return new (Allocate(sizeof(T))) T(parameters...);
        }

        template<typename T>
        void Delete(T *p)
        {
            p->~T();
            Free(p, sizeof(T));
        }

    public:
        virtual ~MemoryManager() {}

        virtual int Initialize();
        virtual void Finalize();
        virtual void Tick();

        void* Allocate(size_t size);
        void Free(void* p, size_t size);

    private:
        inline static size_t*      m_pBlockSizeLookup;
        inline static Allocator*   m_pAllocators;

    private:
        static Allocator* LookupAllocator(size_t size);
    };
}


/*

https://en.m.wikipedia.org/wiki/Memory_management
https://jamesgolick.com/2013/5/19/how-tcmalloc-works.html

tcmalloc: most modern allocators is page-oriented, to reduce fragmentation and increse locality, a page as 8192 bytes.  
    thread caches and central cache. 

*/