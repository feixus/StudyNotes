#pragma once
#include <cstddef>
#include <cstdint>

namespace My
{
    struct BlockHeader {
        BlockHeader* pNext;
    };

    struct PageHeader {
        PageHeader* pNext;
        BlockHeader* Blocks() {
            return reinterpret_cast<BlockHeader*>(this + 1);
        }
    };

    class Allocator {
        public:
            static const uint8_t PATTERN_ALIGN = 0XFC;
            static const uint8_t PATTERN_ALLOC = 0XFD;
            static const uint8_t PATTERN_FREE = 0XFE;

            Allocator();
            Allocator(size_t data_size, size_t page_size, size_t alignment);
            ~Allocator();

            void Reset(size_t data_size, size_t page_size, size_t alignment);

            void* Allocate();
            void Free(void* p);
            void FreeAll();
        
            Allocator(const Allocator& clone) = delete;
            Allocator &operator=(const Allocator &rhs) = delete;

        private:
#if defined(_DEBUG)
            void FillFreePage(PageHeader* pPage);

            void FillFreeBlock(BlockHeader* pBlock);

            void FillAllocateBlock(BlockHeader* pBlock);
#endif

            BlockHeader* NextBlock(BlockHeader* pBlock);

            PageHeader* m_pPageList;

            BlockHeader* m_pFreeList;

            size_t   m_szDataSize;
            size_t   m_szPageSize;
            size_t   m_szAlignmentSize;
            size_t   m_szBlockSize;
            uint32_t m_nBlockPerPage;

            uint32_t m_nPages;
            uint32_t m_nBlocks;
            uint32_t m_nFreeBlocks;
    };


} // namespace My


/*
预先分配资源(如基于块链(block chain)的内存管理), 以减少系统调用  

因使用的对象的尺寸需求不同, Memory Allocator 方式多样化
资源的多样性, 如buff可以用于CPU或者GPU, 高速读取的如贴图, 高速写入的如Rendering Target, 需要保持同步的如Fence  
Allocator 的线程安全性

can use a lookup table for different block size allocators  


http://dmitrysoshnikov.com/compilers/writing-a-memory-allocator/
https://github.com/mtrebi/memory-allocators/tree/master
https://github.com/endurodave/Allocator/tree/master

*/
