#pragma once
#include <memory>
#include <vector>
#include <array>

#include "Fundation.h"
#include "Constexpr.h"
#include "Utils/Misc.h"


namespace MRenderer 
{
    // An object allocator just like vector but with deque memory layout
    // 1. T will be wrapped in a linked list node(@Block), each node will point out the next free node
    // 2. group of nodes lie in a contiguous memory block(@Page)
    // 3. @mPages will grow automatically if more nodes are required
    // 4. capacity expansion won't change the address of existing nodes
    // 5. O(1) time complexity for @Allocate and @Free
    template<typename T>
    class NestedObjectAllocator 
    {

    protected:
        struct Block
        {
        public:
            Block* NextAvaliable;
            T Data;

        public:
            inline static Block* GetBlockPtr(T* data_ptr) 
            {
                return reinterpret_cast<Block*>(reinterpret_cast<uintptr_t>(data_ptr) - MemberAddressOffset(&Block::Data));
            }

            // for the @Block which is occupied, the @NextAvaliable is itself
            bool IsOccupied() const
            {
                return NextAvaliable == this;
            }

            inline T* GetDataPtr()
            {
                return &Data;
            }
        };

        struct Page
        {
            Block* Begin;
            Block* End;
            size_t Capacity;
            Page(Block* buffer, size_t capacity) :
                Begin(buffer), End(buffer + capacity), Capacity(capacity)
            {
            }

            inline size_t Size() const { return Capacity * sizeof(Block);}
            inline Block* Last(){ return Begin + Capacity - 1; }
        };

    public:
        struct AllocatorStats
        {
            size_t Total;
            size_t Occupied;
            size_t Avaliable;
        };

        // The iterator will become invalid if the object it points to is freed. In other cases it remains valid (e.g expansion, allocation or deletion), 
        struct Iterator
        {
            friend class NestedObjectAllocator;
        public:
            static constexpr uint32 Stride = sizeof(Block);

        protected:
            Iterator(NestedObjectAllocator* allocator, uint32 page_index, uint32 element_index):
                PageIndex(page_index), ElementIndex(element_index), Allocator(allocator)
            {
            }

        public:
            Iterator operator++()
            {
                ElementIndex += 1;

                while (PageIndex < Allocator->mPages.size()) 
                {
                    Page& page = Allocator->mPages[PageIndex];
                    while (ElementIndex < page.Capacity)
                    {
                        if (page.Begin[ElementIndex].IsOccupied())
                        {
                            return *this;
                        }
                        else 
                        {
                            ElementIndex++;
                        }
                    }

                    // ok, this page is iterated, we move to the next page
                    PageIndex += 1;
                    ElementIndex = 0;
                }

                *this = End();
                return *this;
            }

            bool operator!=(const Iterator& other) const
            {
                return PageIndex != other.PageIndex || ElementIndex != other.ElementIndex;
            }

            T& operator*()
            {
                ASSERT(*this != End());
                ASSERT(PageIndex < Allocator->mPages.size() && ElementIndex < Allocator->mPages[PageIndex].Capacity);
                
                Block* block = Allocator->mPages[PageIndex].Begin + ElementIndex;

                ASSERT(block->IsOccupied());
                return (Allocator->mPages[PageIndex].Begin + ElementIndex)->Data;
            }

        public:
            static Iterator End()
            {
                return Iterator(nullptr, (std::numeric_limits<uint32>::max)(), (std::numeric_limits<uint32>::max)());
            }

        private:
            uint32 PageIndex;
            uint32 ElementIndex;
            NestedObjectAllocator* Allocator;
        };

        static_assert(sizeof(Iterator) == 16);

    protected:
        static constexpr size_t DEFAULT_CAPACITY = 64;
        static constexpr float MEMORY_EXPAND_FACTOR = 1.5F;

    public:
        NestedObjectAllocator() 
            :mAvaliable(nullptr), mOccupied(0)
        {
        };

        NestedObjectAllocator(const NestedObjectAllocator&) = delete;
        NestedObjectAllocator(NestedObjectAllocator&& other) 
            :NestedObjectAllocator()
        {
            swap(*this, other);
        }

        ~NestedObjectAllocator() 
        {
            for (auto& page  : mPages) 
            {
                _aligned_free(page.Begin);
            }
        };

        NestedObjectAllocator& operator=(NestedObjectAllocator other) 
        {
            swap(*this, other);
            return *this;
        }

        template<typename... Args>
        T* Allocate(Args&&... args) 
        {
            // extend memory if it's insufficient
            if (!mAvaliable) 
            {
                _Expand();
            }
            ASSERT(mAvaliable);

            Block* block = mAvaliable;

            // invoke constructor
            T* data_ptr = block->GetDataPtr();
            new(data_ptr)T(std::forward<Args>(args)...);

            // remove block from the free list
            mAvaliable = mAvaliable->NextAvaliable;
            block->NextAvaliable = block; // the @NextAvaliable pointer of occupied Block points to itself

            mOccupied += 1;
            return data_ptr;
        }

        void Free(T*& data_ptr)
        {
            Block* block = Block::GetBlockPtr(data_ptr);
            ASSERT(_RecycleCheck(block) && "block don't belong to this allocator");

            data_ptr->~T();

            if (!mAvaliable) 
            {
                block->NextAvaliable = nullptr;
                mAvaliable = block;
            }
            else
            {
                block->NextAvaliable = mAvaliable;
                mAvaliable = block;
            }

            mOccupied -= 1;
            data_ptr = nullptr;
        }

        void Clear() 
        {
            for (Page& page : mPages) 
            {
                _ResetLinkage(page);
            }
            mAvaliable = mPages[0].Begin;
            mOccupied = 0;
        }

        AllocatorStats GetStats()
        {   
            AllocatorStats stats{};

            for (Page& page : mPages)
            {
                stats.Total += page.Capacity;
            }

            Block* p = mAvaliable;
            while (p)
            {
                stats.Avaliable += 1;
                p = p->NextAvaliable;
            }

            stats.Occupied = stats.Total - stats.Avaliable;

            ASSERT(stats.Occupied == mOccupied);
            ASSERT(stats.Total == stats.Occupied + stats.Avaliable);

            return stats;
        }

        // how many objects is allocated
        inline uint32 Size() const { return mOccupied; }

        Iterator begin()
        {
            if (mPages.empty()) 
            {
                return Iterator::End();
            }
            else 
            {
                Iterator it(this, 0, 0);
                if (!mPages.empty() && mPages[0].Begin->IsOccupied())
                {
                    return it;
                }
                else
                {
                    // find the next occupied @Block
                    return ++it;
                }
            }
        }

        Iterator end()
        {
            return Iterator::End();
        }

        // check if @obj belongs to this allocator
        bool Validate(T* obj) 
        {
            Block* block = Block::GetBlockPtr(obj);
            return block->IsOccupied() && _RecycleCheck(block);
        }

    protected:
        void _Expand()
        {
            size_t capacity = mPages.empty() ? DEFAULT_CAPACITY : static_cast<size_t>(mPages.back().Capacity * 1.5);

            // allocate memory
            Block* blocks = reinterpret_cast<Block*>(_aligned_malloc(sizeof(Block) * capacity, alignof(Block)));
            memset(blocks, 0, sizeof(Block) * capacity);
            mPages.push_back(Page(blocks, capacity));

            Page& page = mPages.back();
            _ResetLinkage(page);

            if (mAvaliable) 
            {
                page.Last()->NextAvaliable = mAvaliable;
            }
            mAvaliable = page.Begin;
        }

        void _ResetLinkage(Page& page)
        {
            // initialize blocks' linkage
            for (size_t i = 0; i < page.Capacity - 1; i++)
            {
                page.Begin[i].NextAvaliable = page.Begin + i + 1;
            }
            page.Begin[page.Capacity - 1].NextAvaliable = nullptr;
        }

        // check if there's any problem before recycle block
        bool _RecycleCheck(Block* block)
        {
            // pointer in the allocated block should be nullptr
            ASSERT(block->IsOccupied() && "block header contaminated");

            for (auto& page : mPages)
            {
                if (page.Begin <= block and block < page.End) 
                {
                    // ok, that's our block
                    return true;
                }
            }

            return false;
        }

        friend void swap(NestedObjectAllocator& lhs, NestedObjectAllocator& rhs)
        {
            std::swap(lhs.mPages, rhs.mPages);
            std::swap(lhs.mAvaliable, rhs.mAvaliable);
            std::swap(lhs.mOccupied, rhs.mOccupied);
        }

    protected:
        std::vector<Page> mPages;
        Block* mAvaliable;
        uint32 mOccupied;
    };

    // An object allocator just like NestObjectAllocator, it supports allocate range of object additionally
    // it doesn't support recycle object, the only way to recycle object is to call the @Reset function
    // it's used for temporary object allocation during a frame or something will exist permanently
    template<typename T>
        requires std::is_trivially_destructible_v<T>
    class FrameObjectAllocator 
    {
    public:
        struct AllocatorStats
        {
            size_t Total;
            size_t Occupied;
            size_t Avaliable;
        };

    protected:
        struct Page 
        {
            T* Begin;
        };

    public:
        static constexpr uint32 PageSize = 64;

    public:
        FrameObjectAllocator() 
            : mPageIndex(0), mPageOffset(0)
        {
        }

        ~FrameObjectAllocator() 
        {
            for(Page& page : mPages) 
            {
                _aligned_free(page.Begin);
            }
        }

        // doesn't support copy and move for now
        FrameObjectAllocator(const FrameObjectAllocator&) = delete;
        FrameObjectAllocator(FrameObjectAllocator&&) = delete;

        FrameObjectAllocator& operator=(const FrameObjectAllocator&) = delete;
        FrameObjectAllocator& operator=(FrameObjectAllocator&&) = delete;

        template<typename... Args>
        T* Allocate(Args&&... args) 
        {
            ExpandIfFull();

            Page& page = mPages[mPageIndex];
            T* ptr = page.Begin + mPageOffset;
            mPageOffset += 1;

            new(ptr) T(std::forward<Args>(args)...);
            return ptr;
        }

        template<typename... Args>
        T* AllocateRange(uint32 count, Args&&... args) 
        {
            ASSERT(count >= 1);

            // this expansion will ensure the next range allocation lies in the same page
            ExpandIfFull(count);

            T* head = Allocate(std::forward<Args>(args)...);
            for (uint32 i = 1; i < count; i++) 
            {
                Allocate(std::forward<Args>(args)...);
            }
            
            return head;
        }

        void IndexObject(uint32 index) 
        {
            uint32 page_index = index / PageSize;
            uint32 page_offset = index % PageSize;

            ASSERT(page_index < mPages.size() && page_offset < PageSize);
        }

        // warn: destructor won't be invoked during reset
        void Reset() 
        {
            mPageIndex = 0;
            mPageOffset = 0;
        }

        AllocatorStats GetStats()
        {
            AllocatorStats stats;

            stats.Avaliable += PageSize - mPageOffset;
            for (uint32 i = 0; i < mPages.size(); i++) 
            {
                if (i < mPageOffset) 
                {
                    stats.Occupied += mPages[i].Capacity;
                }
                else if(i > mPageOffset)
                {
                    stats.Avaliable += mPages[i].Capacity;
                }
                else 
                {
                    stats.Avaliable += mPages[i].Capacity - mPageOffset;
                    stats.Occupied += mPageOffset;
                }

                stats.Total += mPages[i].Capacity;
            }
            
            return stats;
        }

    protected:
        void ExpandIfFull(uint32 nums_to_allocate=1) 
        {
            // simple assertion to ensure the objects to allocate is less than the page size
            ASSERT(nums_to_allocate < PageSize);

            if (
                mPageIndex == -1 || // uninitialized
                (mPageIndex == mPages.size() - 1 && mPages[mPageIndex].Capacity - mPageOffset < nums_to_allocate) // what's left of the last page isn't enough for this allocation
                )
            {
                // allocate new page
                Page* page = _aligned_malloc(sizeof(T) * PageSize, alignof(T));
                mPages.push_back(Page{ page });

                // if the avaliable space is not enough, we will move on to the next page
                mPageIndex += 1;
                mPageOffset = 0;
            }

            ASSERT(mPages.size() > mPageIndex && mPageOffset < mPages[mPageIndex].Capacity);
        }

    protected:
        std::vector<Page> mPages;
        int mPageIndex;
        int mPageOffset;
    };

    struct ObjectHandle 
    {
        static constexpr uint32 MaxPageSize = (std::numeric_limits<uint16>::max)();
        static constexpr uint32 MaxPageNumber = (std::numeric_limits<uint16>::max)();

        uint16 page_index;
        uint16 offset;
    };


    // same things as LinearObjectAllocator, but this one don't holds any kind of memory resource.
    // object are allocated in the form of @ObjectHandle, which record the page index and offset
    class FrameObjectAllocatorMeta
    {
    public:
        explicit FrameObjectAllocatorMeta(uint32 page_size)
            :mPageCapacity(page_size)
        {
            ASSERT(page_size <= ObjectHandle::MaxPageSize);
        }

        ObjectHandle Allocate() 
        {
            if ((mPageCount == 0) || (mOffset == mPageCapacity))
            {
                NextPage();
            }

            return ObjectHandle{ mPageIndex, mOffset++ };
        }

        // allocate memory contiguous object
        ObjectHandle AllocateRange(uint32 size)
        {
            ASSERT(size > 0 && size < mPageCapacity);

            if ((mPageCount == 0) || (mOffset + size > mPageCapacity))
            {
                NextPage();
            }

            ObjectHandle ret{ mPageIndex, mOffset };

            mOffset += size;
            return ret;
        }

        void Reset()
        {
            mPageIndex = 0;
            mOffset = 0;
        }

    protected:
        void NextPage() 
        {
            ASSERT(mPageCount <= ObjectHandle::MaxPageNumber);

            if (mPageCount == 0) 
            {
                // initialization
                mPageIndex = 0;
            }
            else 
            {
                // out of page
                mPageIndex++;
            }

            mOffset = 0;
            if (mPageIndex == mPageCount)
            {
                mPageCount++;
            }
        }

    protected:
        uint16 mPageIndex = 0;
        uint16 mPageCount = 0;
        uint16 mOffset = 0;
        uint16 mPageCapacity;
    };

    // same things as NestedObjectAllocator, but the actual object allocation happens in a separate class
    // object are allocated in the form of @ObjectHandle, which record the page index and offset
    class RandomObjectAllocatorMeta
    {
    public:
        RandomObjectAllocatorMeta(uint32 page_size)
            :mPageCapacity(page_size), mPageCount(0)
        {
            ASSERT(page_size <= ObjectHandle::MaxPageSize);
        }

        ObjectHandle Allocate()
        {
            if (mFreeNodes.empty())
            {
                ASSERT(mPageCount != ObjectHandle::MaxPageNumber);

                for (uint16 i = 0; i < mPageCapacity; i++)
                {
                    mFreeNodes.push_back({ mPageCount, i });
                }
                mPageCount++;
            }

            ObjectHandle node = mFreeNodes.back();
            mFreeNodes.pop_back();
            return ObjectHandle(node.page_index, node.offset);
        }

        ObjectHandle Free(const ObjectHandle& handle)
        {
            mFreeNodes.push_back(handle);
            return handle;
        }

        inline uint32 PageCount() const
        {
            return mPageCount;
        }

    private:
        uint16 mPageCapacity;
        uint16 mPageCount;
        std::vector<ObjectHandle> mFreeNodes;
    };
    // ref: https://www.zhihu.com/search?type=content&q=TLSF
    // this class only for memory management, does not actually hold any resource
    template<uint32_t MinBlockSize = 256, uint32_t FirstLevel = 32, uint32_t SecondLevel = 5>
        requires (
    // due to the bitmap limitation, the numbers of first or second level buckets should be not greater than 32.
    // for the first level, N = @FirstLevel <= 32
    // for the second level, N = 2 ^ @SecondLevel <= 32, SecondLevel <= 6
    FirstLevel <= 32 && FirstLevel > SecondLevel
        && ((1 << (SecondLevel - 1)) <= sizeof(uint32_t) * CHAR_BIT))
        class TLSFMeta
    {
    private:
        struct Block
        {
            uint32_t Offset; // offset on the heap
            uint32_t Size;

            Block* PrePhysical;
            Block* NextPhysical; // physical list
            Block* PreFree;
            Block* NextFree; // segregated lists

            inline bool IsFree() const
            {
                return PreFree != this;
            }
        };

    public:
        struct Allocation
        {
            friend TLSFMeta;
        public:
            uint32_t Offset;
            uint32_t Size;
            uint32_t Alignment;

        private:
            Block* BlockPtr;
            TLSFMeta* Source;
        };

        struct Stats
        {
            size_t AllocatedMemory;
            size_t FreeMemory;
            size_t BackupMemory;
            size_t PhysicalOccupiedBlock;
            size_t PhysicalFreeBlock;
            NestedObjectAllocator<Block>::AllocatorStats BlockAllocatorStats;
            NestedObjectAllocator<Allocation>::AllocatorStats AllocationAllocatorStats;
        };

    public:
        explicit TLSFMeta(uint32_t size)
        {
            mFreeOffset = 0;
            mSize = size;
        }

        TLSFMeta(const TLSFMeta& other) = delete;

        TLSFMeta(TLSFMeta&& other)
            :TLSFMeta(0)
        {
            swap(*this, other);
        }

        TLSFMeta& operator=(TLSFMeta other)
        {
            swap(*this, other);
            return *this;
        }

        friend void swap(TLSFMeta& lhs, TLSFMeta& rhs)
        {
            using std::swap;
            swap(lhs.mSize, rhs.mSize);
            swap(lhs.mFreeOffset, rhs.mFreeOffset);
            swap(lhs.mPhysicalFirst, rhs.mPhysicalFirst);
            swap(lhs.mPhysicalLast, rhs.mPhysicalLast);
            swap(lhs.mBlockAllocator, rhs.mBlockAllocator);
            swap(lhs.mAllocationAllocator, rhs.mAllocationAllocator);
            swap(lhs.mFreeList, rhs.mFreeList);
        }

        Allocation* Allocate(uint32_t size, uint32_t alignment)
        {
            ASSERT(size < mSize);
            ASSERT((alignment & (alignment - 1)) == 0); // alignment must be power of 2, or AlignUp will malfunction
            ASSERT(size >= MinBlockSize);

            // find a free block
            Block* block_ptr = FindFreeBlock(size, alignment);
            if (!block_ptr)
            {
                return nullptr;
            }

            RemoveBlock(block_ptr);

            uint32_t begin = block_ptr->Offset;
            uint32_t align_left = AlignUp(block_ptr->Offset, alignment);
            uint32_t align_right = align_left + AlignUp(size, alignment);
            uint32_t end = block_ptr->Offset + block_ptr->Size;
            ASSERT(align_right <= end);

            // there are some space unused due to memory alignment, split it into smaller part for later usage
            if (align_left - begin >= MinBlockSize)
            {
                Block* split = mBlockAllocator.Allocate();

                split->Offset = begin;
                split->Size = align_left - begin;
                block_ptr->Offset = align_left;
                block_ptr->Size -= split->Size;

                split->PrePhysical = block_ptr->PrePhysical;
                split->NextPhysical = block_ptr;
                block_ptr->PrePhysical = split;

                if (split->PrePhysical)
                {
                    split->PrePhysical->NextPhysical = split;
                }

                InsertBlock(split);

                if (block_ptr == mPhysicalFirst)
                {
                    mPhysicalFirst = split;
                }
            }

            // the block is too large for this allocation, split the rest part into a minor block
            if (end - align_right >= MinBlockSize)
            {
                Block* split = mBlockAllocator.Allocate();

                split->Size = end - align_right;
                split->Offset = align_right;
                block_ptr->Size -= split->Size;

                split->PrePhysical = block_ptr;
                split->NextPhysical = block_ptr->NextPhysical;
                block_ptr->NextPhysical = split;

                if (split->NextPhysical)
                {
                    split->NextPhysical->PrePhysical = split;
                }

                InsertBlock(split);

                if (block_ptr == mPhysicalLast)
                {
                    mPhysicalLast = split;
                }
            }

            // ok, we are good to go
            Allocation* allocation_ptr = mAllocationAllocator.Allocate();
            allocation_ptr->Offset = AlignUp(block_ptr->Offset, alignment);
            allocation_ptr->Size = size;
            allocation_ptr->BlockPtr = block_ptr;
            allocation_ptr->Source = this;
            allocation_ptr->Alignment = alignment;

            return allocation_ptr;
        }

        void Free(Allocation* allocation_ptr)
        {
            // check if the allocation belong to this one and is it still valid
            ASSERT(allocation_ptr && allocation_ptr->Source == this && allocation_ptr->Offset + allocation_ptr->Size <= mSize
                && allocation_ptr->BlockPtr && !allocation_ptr->BlockPtr->IsFree() && allocation_ptr->BlockPtr->Offset + allocation_ptr->BlockPtr->Size <= mSize);
            Block* block_ptr = allocation_ptr->BlockPtr;

            // merge with the previous block if it's free
            if (block_ptr->PrePhysical && block_ptr->PrePhysical->IsFree())
            {
                Block* prev_block = block_ptr->PrePhysical;
                if (prev_block == mPhysicalFirst)
                {
                    mPhysicalFirst = block_ptr;
                }

                block_ptr->PrePhysical = prev_block->PrePhysical;
                if (prev_block->PrePhysical)
                {
                    prev_block->PrePhysical->NextPhysical = block_ptr;
                }

                block_ptr->Offset = prev_block->Offset;
                block_ptr->Size += prev_block->Size;

                RemoveBlock(prev_block);
                mBlockAllocator.Free(prev_block);
            }

            // merge with next block if it's free
            if (block_ptr->NextPhysical && block_ptr->NextPhysical->IsFree())
            {
                Block* next_block = block_ptr->NextPhysical;
                if (next_block == mPhysicalLast)
                {
                    mPhysicalLast = block_ptr;
                }

                block_ptr->NextPhysical = next_block->NextPhysical;
                if (next_block->NextPhysical)
                {
                    next_block->NextPhysical->PrePhysical = block_ptr;
                }

                block_ptr->Size += next_block->Size;

                RemoveBlock(next_block);
                mBlockAllocator.Free(next_block);
            }

            InsertBlock(block_ptr);

            mAllocationAllocator.Free(allocation_ptr);
        }

        inline uint32_t MaxAllocationSize() const
        {
            return Min(1U << (FirstLevel - 1), mSize);
        }

        inline uint32_t Size()
        {
            return mSize;
        }

        inline void Reset()
        {
            *this = std::move(TLSFMeta(mSize));
        }

        Stats GetStats()
        {
            size_t allocated_block = 0;
            size_t free_block = 0;
            size_t free_space = 0;
            for (int i = 0; i < FirstLevel * (1 << SecondLevel); i++)
            {
                for (Block* p = mFreeList[i]; p; p = p->NextFree)
                {
                    if (p->IsFree())
                    {
                        free_space += p->Size;
                    }
                }
            }

            size_t allocated = 0;
            size_t free_space2 = 0;
            for (Block* p = mPhysicalFirst; p; p = p->NextPhysical)
            {
                if (p->IsFree())
                {
                    free_space2 += p->Size;
                    free_block++;
                }
                else
                {
                    allocated += p->Size;
                    allocated_block++;
                }
            }

            ASSERT(free_space == free_space2);

            return Stats{
                .AllocatedMemory = allocated,
                .FreeMemory = free_space,
                .BackupMemory = mSize - mFreeOffset,
                .PhysicalOccupiedBlock = allocated_block,
                .PhysicalFreeBlock = free_block,
                .BlockAllocatorStats = mBlockAllocator.GetStats(),
                .AllocationAllocatorStats = mAllocationAllocator.GetStats()
            };
        }

    protected:
        Block* FindFreeBlock(uint32_t size, uint32_t alignment)
        {
            // best match bucket
            auto [best_fli, best_sli] = Mapping(size);

            // search buckets from the best match one
            uint32_t fli_map = mBitMapFli & (~0u << best_fli); // bitmap of all fli ¡Ý best_fli
            while (fli_map)
            {
                // bitmap indicate where the free blocks are
                uint32_t fli = FFS(fli_map);
                fli_map &= ~(1u << fli);

                uint32_t sli_map = mBitMapSli[fli];
                if (fli == best_fli)
                {
                    sli_map &= (~0u << best_sli); // mask out all sli < best_sli when in the first fli
                }

                // iterate over second-level buckets indicated by sli_map
                while (sli_map)
                {
                    uint32_t sli = FFS(sli_map);
                    sli_map &= ~(1u << sli);

                    // search all blocks in this bucket, see if it satisfy the requirement
                    for (Block* block = mFreeList[MakeIndex(fli, sli)]; block; block = block->NextFree)
                    {
                        if (CheckBlock(block, size, alignment))
                            return block;
                    }
                }
            }

            // nothing is found, try to make a new block
            Block* block_ptr = MakeNewBlock(size, alignment);
            if (block_ptr)
            {
                return block_ptr;
            }

            // out of memory
            return nullptr;
        }

        // check if the given block satisfy the required size and alignment.
        inline bool CheckBlock(Block* block_ptr, uint32_t size, uint32_t alignment)
        {
            uint32_t required_size = AlignUp(block_ptr->Offset, alignment) - block_ptr->Offset + AlignUp(size, alignment);
            return block_ptr->Size >= required_size;
        }

        void RemoveBlock(Block* block_ptr)
        {
            ASSERT(block_ptr->IsFree());

            // remove block from the segregated list
            if (block_ptr->PreFree)
            {
                block_ptr->PreFree->NextFree = block_ptr->NextFree;
            }

            if (block_ptr->NextFree)
            {
                block_ptr->NextFree->PreFree = block_ptr->PreFree;
            }

            // PreFree pointer of occupied block is itself
            block_ptr->PreFree = block_ptr;

            auto [fli, sli] = Mapping(block_ptr->Size);
            uint32_t index = MakeIndex(fli, sli);

            if (mFreeList[index] == block_ptr)
            {
                mFreeList[index] = block_ptr->NextFree;
            }

            if (!mFreeList[index])
            {
                mBitMapSli[fli] &= ~(1 << sli);

                if (mBitMapSli[fli] == 0)
                {
                    mBitMapFli &= ~(1 << fli);
                }
            }
        }

        void InsertBlock(Block* block_ptr)
        {
            // insert block as the head of the segregated list
            auto [fli, sli] = Mapping(block_ptr->Size);

            block_ptr->PreFree = nullptr;
            block_ptr->NextFree = mFreeList[MakeIndex(fli, sli)];
            if (block_ptr->NextFree)
            {
                block_ptr->NextFree->PreFree = block_ptr;
            }
            mFreeList[MakeIndex(fli, sli)] = block_ptr;

            // update bit map
            mBitMapFli |= 1 << fli;
            mBitMapSli[fli] |= 1 << sli;
        }

        Block* MakeNewBlock(uint32_t size, uint32_t alignment)
        {
            // the alignment may not match up with @mFreeOffset, extra memory will be required if this happen
            size = AlignUp(mFreeOffset, alignment) - mFreeOffset + AlignUp(size, alignment);

            if (size > (mSize - mFreeOffset))
            {
                // no space left
                return nullptr;
            }

            Block* block_ptr = mBlockAllocator.Allocate();
            block_ptr->Offset = mFreeOffset;
            block_ptr->Size = size;
            block_ptr->PreFree = nullptr;
            block_ptr->NextFree = nullptr;

            mFreeOffset = mFreeOffset + size;
            if (!mPhysicalLast && !mPhysicalFirst)
            {
                mPhysicalFirst = mPhysicalLast = block_ptr;
                block_ptr->PrePhysical = nullptr;
                block_ptr->NextPhysical = nullptr;
            }
            else if (mPhysicalLast && mPhysicalFirst)
            {
                mPhysicalLast->NextPhysical = block_ptr;
                block_ptr->PrePhysical = mPhysicalLast;
                block_ptr->NextPhysical = nullptr;
                mPhysicalLast = block_ptr;
            }
            else
            {
                ASSERT("Unexpected Code");
            }

            InsertBlock(block_ptr);
            return block_ptr;
        }

    protected:
        static std::pair<uint32_t, uint32_t> Mapping(uint32_t size)
        {
            ASSERT(size);

            if (size < (1 << SecondLevel))
            {
                uint32_t fli = 0;
                uint32_t sli = size;

                return std::pair(fli, sli);
            }
            else
            {
                uint32_t fli = FLS(size);
                uint32_t sli = (size >> (fli - SecondLevel)) & ((1U << SecondLevel) - 1);
                return std::pair(fli, sli);
            }
        }

        static inline uint32_t MakeIndex(uint32_t fli, uint32_t sli)
        {
            return fli * (1 << SecondLevel) + sli;
        }

        // size to index of the segregated list
        static inline uint32_t MakeSizeIndex(uint32_t size, uint32_t alignment)
        {
            auto [fli, sli] = Mapping(size);
            return MakeIndex(fli, sli);
        }

    protected:
        NestedObjectAllocator<Block> mBlockAllocator;
        NestedObjectAllocator<Allocation> mAllocationAllocator;

        std::array<Block*, FirstLevel* (1 << SecondLevel)> mFreeList = {};
        Block* mPhysicalFirst = nullptr; // head of the physical list
        Block* mPhysicalLast = nullptr; // tail of the physical list

        std::array<uint32_t, FirstLevel> mBitMapSli = {}; // second level bitmap, same as @mBitMapFli
        uint32_t mBitMapFli = 0; // first level bitmap, 0 means the bucket is empty, 1 means the bucket has free blocks
        uint32_t mFreeOffset = 0; // [mFreeOffset, mSize) is the area never been allocated
        uint32_t mSize = 0;
    };

}