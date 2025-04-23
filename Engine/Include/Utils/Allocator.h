#pragma once
#include <memory>
#include <deque>
#include <iostream>
#include <vector>

#include "Fundation.h"
#include "Constexpr.h"
#include "Utils/Misc.h"


namespace MRenderer 
{
    // An object allocator just like vector but with deque memory layout
    // 1. T will be wrapped in a linked list node(@Block), each node will point out the next free node
    // 2. group of nodes lie in a contiguous memory block(@Chunk)
    // 3. @mChunks will grow automatically if more nodes are required
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

        struct Chunk
        {
            Block* Begin;
            Block* End;
            size_t Capacity;
            Chunk(Block* buffer, size_t capacity) :
                Begin(buffer), End(buffer + capacity * sizeof(Block)), Capacity(capacity)
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
            inline static const Iterator End = Iterator(nullptr, -1, -1);

        protected:
            Iterator(NestedObjectAllocator* allocator, uint32 chunk_index, uint32 element_index):
                ChunkIndex(chunk_index), ElementIndex(element_index), Allocator(allocator)
            {
            }

        public:
            Iterator operator++()
            {
                ElementIndex += 1;

                while (ChunkIndex < Allocator->mChunks.size()) 
                {
                    Chunk& chunk = Allocator->mChunks[ChunkIndex];
                    while (ElementIndex < chunk.Capacity)
                    {
                        if (chunk.Begin[ElementIndex].IsOccupied())
                        {
                            return *this;
                        }
                        else 
                        {
                            ElementIndex++;
                        }
                    }

                    // ok, this chunk is iterated, we move to the next chunk
                    ChunkIndex += 1;
                    ElementIndex = 0;
                }

                *this = End;
                return *this;
            }

            bool operator!=(const Iterator& other) const
            {
                return ChunkIndex != other.ChunkIndex || ElementIndex != other.ElementIndex;
            }

            T& operator*()
            {
                ASSERT(*this != End);
                ASSERT(ChunkIndex < Allocator->mChunks.size() && ElementIndex < Allocator->mChunks[ChunkIndex].Capacity);
                
                Block* block = Allocator->mChunks[ChunkIndex].Begin + ElementIndex;

                ASSERT(block->IsOccupied());
                return (Allocator->mChunks[ChunkIndex].Begin + ElementIndex)->Data;
            }

        private:
            uint32 ChunkIndex;
            uint32 ElementIndex;
            NestedObjectAllocator* Allocator;
        };

        static_assert(sizeof(Iterator) == 16);

    protected:
        static constexpr size_t DEFAULT_CAPACITY = 64;
        static constexpr float MEMORY_EXPAND_FACTOR = 1.5F;

    public:
        NestedObjectAllocator() 
            :mAvaliable(nullptr), mSize(0)
        {
        };

        NestedObjectAllocator(const NestedObjectAllocator&) = delete;
        NestedObjectAllocator(NestedObjectAllocator&& other) 
            :NestedObjectAllocator()
        {
            Swap(*this, other);
        }

        ~NestedObjectAllocator() 
        {
            for (auto& chunk  : mChunks) 
            {
                _aligned_free(chunk.Begin);
            }
        };

        NestedObjectAllocator& operator=(NestedObjectAllocator other) 
        {
            Swap(*this, other);
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

            mAvaliable = mAvaliable->NextAvaliable;
            block->NextAvaliable = block; // the @NextAvaliable pointer of occupied Block points to itself

            // invoke constructor
            T* data_ptr = block->GetDataPtr();
            new(data_ptr)T(std::forward<Args>(args)...);

            mSize += 1;
            return data_ptr;
        }

        void Free(T* data_ptr)
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

            mSize -= 1;
        }

        template<typename U>
            requires requires(U t) { std::same_as<std::decay<decltype(t.next)*>, T*>; }
        void FreeList(U*& ptr)
        {
            while (ptr)
            {
                U* next = ptr->next;
                Free(ptr);
                ptr = next;
            }
        }

        void Clear() 
        {
            for (Chunk& chunk : mChunks) 
            {
                _ResetLinkage(chunk);
            }
            mAvaliable = mChunks[0].Begin;
            mSize = 0;
        }

        AllocatorStats GetStats()
        {   
            AllocatorStats stats{};

            for (Chunk& chunk : mChunks)
            {
                stats.Total += chunk.Capacity;
            }

            Block* p = mAvaliable;
            while (p)
            {
                stats.Avaliable += 1;
                p = p->NextAvaliable;
            }

            stats.Occupied = stats.Total - stats.Avaliable;

            ASSERT(stats.Occupied == mSize);
            ASSERT(stats.Total == stats.Occupied + stats.Avaliable);

            return stats;
        }

        // how many objects is allocated
        inline uint32 Size() const { return mSize; }

        Iterator begin()
        {
            if (mChunks.empty()) 
            {
                return Iterator::End;
            }
            else 
            {
                Iterator it(this, 0, 0);
                if (!mChunks.empty() && mChunks[0].Begin->IsOccupied())
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
            return Iterator::End;
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
            size_t capacity = mChunks.empty() ? DEFAULT_CAPACITY : static_cast<size_t>(mChunks.back().Capacity * 1.5);

            // allocate memory
            Block* blocks = reinterpret_cast<Block*>(_aligned_malloc(sizeof(Block) * capacity, alignof(Block)));
            memset(blocks, 0, sizeof(Block) * capacity);
            mChunks.push_back(Chunk(blocks, capacity));

            Chunk& chunk = mChunks.back();
            _ResetLinkage(chunk);

            if (!mAvaliable) 
            {
                mAvaliable = chunk.Begin;
            }
            else 
            {
                chunk.Last()->NextAvaliable = mAvaliable;
                mAvaliable = chunk.Begin;
            }
        }

        void _ResetLinkage(Chunk& chunk) 
        {
            // initialize blocks' linkage
            for (size_t i = 0; i < chunk.Capacity - 1; i++)
            {
                chunk.Begin[i].NextAvaliable = chunk.Begin + i + 1;
            }
            chunk.Begin[chunk.Capacity - 1].NextAvaliable = nullptr;
        }

        // check if there's any problem before recycle block
        bool _RecycleCheck(Block* block)
        {
            // pointer in the allocated block should be nullptr
            ASSERT(block->IsOccupied() && "block header contaminated");

            for (auto& chunk : mChunks)
            {
                if (chunk.Begin <= block and block < chunk.End) 
                {
                    // ok, that's our block
                    return true;
                }
            }

            return false;
        }

        friend void Swap(NestedObjectAllocator& lhs, NestedObjectAllocator& rhs)
        {
            std::swap(lhs.mChunks, rhs.mChunks);
            std::swap(lhs.mAvaliable, rhs.mAvaliable);
            std::swap(lhs.mSize, rhs.mSize);
        }

    protected:
        std::vector<Chunk> mChunks;
        Block* mAvaliable;
        uint32 mSize;
    };

    // An object allocator just like NestObjectAllocator, it supports allocate range of object additionally
    // it doesn't support recycle object, the only way to recycle object is to call the @Reset function
    // it's used for temporary object allocation during a frame or something will exist permanently
    template<typename T>
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

            stats.Avaliable += mPages[mPageIndex].Capacity - mPageOffset;
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
        void ExpandIfFull(uint32 nums_to_allocate) 
        {
            // simple assertion to ensure the objects to allocate is less than the page size
            ASSERT(nums_to_allocate < PageSize);

            if (
                mPageIndex == -1 || // uninitialized
                (mPageIndex == mPages.size() - 1 && mPages[mPageIndex].Capacity - mPageOffset < nums_to_allocate) // what's left of the last page isn't enough for this allocation
                )
            {
                // allocate new page
                Page* page = _aligned_malloc(sizeof(Page) * PageSize, alignof(Page));
                mPages.push_back(Page{ page });

                // if the avaliable space is not enough, we will move on to the next page
                mPageIndex += 1;
                mPageOffset = 0;
            }

            ASSERT(mPages.size() > mPageIndex && mPageOffset < mPages[mPageIndex].Capacity);
        }

    protected:
        std::vector<Page> mPages;
        uint32 mPageIndex;
        uint32 mPageOffset;

    };

    struct ObjectHandle 
    {
        static constexpr uint32 MaxPageSize = UINT16_MAX;
        static constexpr uint32 MaxPageNumber = UINT16_MAX;

        uint16 page_index;
        uint16 offset;
    };


    // Same things as LinearObjectAllocator, but the actual object allocation happens in a separate class
    // Object are allocated in the form of @ObjectHandle, which record the page index and offset
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

    // Same things as NestedObjectAllocator, but the actual object allocation happens in a separate class
    // Object are allocated in the form of @ObjectHandle, which record the page index and offset
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


    // this class only for memory management, does not actually hold any resource
    template<uint32_t MinBlockSize = 256, uint32_t FirstLevel = 32, uint32_t SecondLevel = 5>
    requires (
        FirstLevel <= 32 && FirstLevel > SecondLevel // bit map is represented by uint32, the first level index needs to be no larger than 32
        && ((1 << (SecondLevel - 1)) <= sizeof(uint32_t) * CHAR_BIT))// SecondLevel needs to be less than 6, the numbers of second level index is 2^SecondLevel, it needs to be no larger than 32
    class TLSFMeta 
    {
    private:
        struct Block
        {
            uint32_t offset; // offset on the heap
            uint32_t size;

            Block* pre_physical;
            Block* next_physical; // physical list
            Block* pre_free;
            Block* next_free; // segragated listS

            inline void MarkFree() 
            {
                pre_free = nullptr;
            }

            inline void MarkTaken() 
            {
                pre_free = this;
            }

            inline bool IsFree() const
            {
                return pre_free != this;
            }
        };

    public:
        struct Allocation 
        {
            friend TLSFMeta;
        public:
            uint32_t offset;
            uint32_t size;
            uint32_t alignment;

        private:
            Block* block;
            TLSFMeta* source;
        };

        struct Stats 
        {
            size_t allocated_memory;
            size_t free_memory;
            size_t unallocated_memory;
            size_t allocated_block;
            size_t free_block;
            NestedObjectAllocator<Block>::AllocatorStats block_allocator_stats;
            NestedObjectAllocator<Allocation>::AllocatorStats allocation_allocator_stats;
        };

    public:
        explicit TLSFMeta(uint32_t size)
        {
            mFreeOffset = 0; 
            mSize = size;
        }

        Allocation* Allocate(uint32_t size, uint32_t alignment)
        {
            if (size > mSize) 
            {
                return nullptr;
            }

            ASSERT((alignment & (alignment - 1)) == 0 && alignment <= size); // alignment must be power of 2, or AlignUp will malfunction

            // find a free block
            Block* block = FindFreeBlock(size, alignment);
            if (!block) 
            {
                return nullptr;
            }

            RemoveBlock(block);

            uint32_t begin = block->offset;
            uint32_t align_left = AlignUp(block->offset, alignment);
            uint32_t align_right = align_left + AlignUp(size, alignment);
            uint32_t end = block->offset + block->size;

            // there are some space unused due to memory alignment, split it into smaller part for later usage
            if (align_left - begin >= MinBlockSize) 
            {
                Block* split = mBlockAllocator.Allocate();
                
                split->offset = begin;
                split->size = align_left - begin;
                block->offset = align_left;
                block->size -= split->size;

                split->pre_physical = block->pre_physical;
                split->next_physical = block;
                block->pre_physical = split;

                if (split->pre_physical)
                {
                    split->pre_physical->next_physical = split;
                }

                InsertBlock(split);

                if (block == mFirst) 
                {
                    mFirst = block;
                }
            }

            // the block is to large for this allocation, split the rest part into a minor block
            if (end - align_right >= MinBlockSize)
            {
                Block* split = mBlockAllocator.Allocate();
                
                split->size = end - align_right;
                split->offset = align_right;
                block->size -= split->size;

                
                split->pre_physical = block;
                split->next_physical = block->next_physical;
                block->next_physical = split;

                if (split->next_physical) 
                {
                    split->next_physical->pre_physical = split;
                }

                InsertBlock(split);

                if (block == mLast) 
                {
                    mLast = block;
                }
            }

            // ok, we are good to go
            Allocation* allocation = mAllocationAllocator.Allocate();
            allocation->offset = AlignUp(block->offset, alignment);
            allocation->size = size;
            allocation->block = block;
            allocation->source = this;
            allocation->alignment = alignment;
            ASSERT(allocation->offset + size <= block->offset + block->size);

            return allocation;
        }

        void Free(Allocation* allocation) 
        {
            // check if the allocation belong to this one and is it still valid
            ASSERT(allocation && allocation->source == this && allocation->offset + allocation->size <= mSize
                && allocation->block && !allocation->block->IsFree() && allocation->block->offset + allocation->block->size <= mSize);
            Block* block = allocation->block;

            // merge with the previous block if it's free
            if (block->pre_physical && block->pre_physical->IsFree())
            {
                Block* prev_block = block->pre_physical;
                if (prev_block == mFirst) 
                {
                    mFirst = block;
                }

                block->pre_physical = prev_block->pre_physical;
                if (prev_block->pre_physical)
                {
                    prev_block->pre_physical->next_physical = block;
                }

                block->offset = prev_block->offset;
                block->size += prev_block->size;
                
                RemoveBlock(prev_block);
                mBlockAllocator.Free(prev_block);
            }

            // merge with next block if it's free
            if (block->next_physical && block->next_physical->IsFree())
            {
                Block* next_block = block->next_physical;
                if (next_block == mLast) 
                {
                    mLast = block;
                }

                block->next_physical = next_block->next_physical;
                if (next_block->next_physical)
                {
                    next_block->next_physical->pre_physical = block;
                }

                block->size += next_block->size;

                RemoveBlock(next_block);
                mBlockAllocator.Free(next_block);
            }
            
            InsertBlock(block);

            mAllocationAllocator.Free(allocation);
        }

        inline uint32_t MaxAllocationSize() const
        {
            return 1U << (FirstLevel - 1) < mSize ? 1U << (FirstLevel - 1) : mSize;
        }

        inline uint32_t Size()
        {
            return mSize;
        }

        Stats GetStats() 
        {
            size_t allocated_block = 0;
            size_t free_block = 0;
            size_t free_space = 0;
            for (int i = 0; i < FirstLevel * (1 << SecondLevel); i++) 
            {
                for (Block* p = mFreeList[i]; p; p = p->next_free) 
                {
                    if (p->IsFree()) 
                    {
                        free_space += p->size;
                    }
                }
            }

            size_t allocated = 0;
            size_t free_space_2 = 0;
            for (Block* p = mFirst; p; p = p->next_physical) 
            {
                if (p->IsFree()) 
                {
                    free_space_2 += p->size;
                    free_block++;
                }
                else 
                {
                    allocated += p->size;
                    allocated_block++;
                }
            }
            
            ASSERT(free_space == free_space_2);

            return Stats{ allocated, free_space, mSize - mFreeOffset, allocated_block, free_block, mBlockAllocator.GetStats(), mAllocationAllocator.GetStats()};
        }

    protected:
        Block* FindFreeBlock(uint32_t size, uint32_t alignment)
        {
            auto [fli, sli] = Mapping(size);
            uint32_t bucket_index = MakeIndex(fli, sli);
            
            // best match
            for (Block* p = mFreeList[bucket_index]; p != nullptr; p = p->next_free)
            {
                if (CheckBlock(p, size, alignment)) 
                {
                    return p;
                }
            }

            // nothing found, try to make a new block
            Block* block = MakeNewBlock(size, alignment);
            if (block) 
            {
                return block;
            }

            // search larger bucket, see if there's any blocks left
            for (uint32_t i = bucket_index + 1; i < FirstLevel * (1 << SecondLevel); i++) 
            {
                Block* head = mFreeList[i];
                while (head) 
                {
                    if (CheckBlock(head, size, alignment)) 
                    {
                        return head;
                    }
                    head = head->next_free;
                }
            }

            // out of memory
            return nullptr;
        }

        inline bool CheckBlock(Block* block, uint32_t size, uint32_t alignment)
        {
            //it's sufficient for the offset to align with the alignment. Aligning the size is optional
            uint32_t required_size = AlignUp(block->offset, alignment) - block->offset + size;
            return block->size >= required_size;
        }

        void RemoveBlock(Block* block)
        {
            ASSERT(block->IsFree());
        
            // remove block from the segragated list
            if (block->pre_free)
            {
                block->pre_free->next_free = block->next_free;
            }

            if (block->next_free)
            {
                block->next_free->pre_free = block->pre_free;
            }
            block->MarkTaken();

            auto [fli, sli] = Mapping(block->size);
            uint32_t index = MakeIndex(fli, sli);

            if (mFreeList[index] == block) 
            {
                mFreeList[index] = block->next_free;
                if (!mFreeList[index]) 
                {
                    mBitMapSLI[fli] &= 1 << sli;
                    mBitMapFLI &= ~(1 << fli);
                }
            }
        }

        void InsertBlock(Block* block)
        {
            block->MarkFree();

            // insert block as the head of the segregated list
            auto [fli, sli] = Mapping(block->size);

            block->pre_free = nullptr;
            block->next_free = mFreeList[MakeIndex(fli, sli)];
            if (block->next_free)
            {
                block->next_free->pre_free = block;
            }
            mFreeList[MakeIndex(fli, sli)] = block;

            // update bit map
            mBitMapFLI |= 1 << fli;
            mBitMapSLI[fli] |= 1 << sli;
        }

        Block* MakeNewBlock(uint32_t size, uint32_t alignment)
        {
            // make size and offset match up with the alignment
            // aligning size with alignment is optional
            uint32_t offset = AlignUp(mFreeOffset, alignment);
            size = AlignUp(size, alignment) + offset - mFreeOffset; 

            if (size > (mSize - mFreeOffset)) 
            {
                // no space left
                return nullptr;
            }

            Block* block = mBlockAllocator.Allocate();
            block->offset = offset;
            block->size = size;
            block->pre_free = nullptr;
            block->next_free = nullptr;
            
            mFreeOffset = offset + size;
            if (!mLast && !mFirst) 
            {
                mFirst = mLast = block;
                block->pre_physical = nullptr;
                block->next_physical = nullptr;
            }
            else if(mLast && mFirst)
            {
                mLast->next_physical = block;
                block->pre_physical = mLast;
                block->next_physical = nullptr;
                mLast = block;
            }
            else 
            {
                ASSERT("Unexpected Code");
            }

            InsertBlock(block);
            return block;
        }
    
    protected:
        static std::pair<uint32_t, uint32_t> Mapping(uint32_t size) 
        {
            uint32_t fli = FLS(size);
            uint32_t sli = (size >> (fli - SecondLevel)) ^ (1 << SecondLevel);
            return std::pair(fli, sli);
        }

        static inline uint32_t MakeIndex(uint32_t fli, uint32_t sli) 
        {
            return fli * SecondLevel + sli;
        }

        // size to index of the segregated list
        static inline uint32_t MakeSizeIndex(uint32_t size, uint32_t alignemnt) 
        {
            auto [fli, sli] = Mapping(size);
            return MakeIndex(fli, sli);
        }

    protected:
        NestedObjectAllocator<Block> mBlockAllocator;
        NestedObjectAllocator<Allocation> mAllocationAllocator;

        Block* mFreeList[FirstLevel * (1 << SecondLevel)] = {};
        Block* mLast = nullptr; // head of the physical list
        Block* mFirst = nullptr; // tail of the physical list

        uint32_t mBitMapSLI[FirstLevel] = {};
        uint32_t mBitMapFLI = 0;
        uint32_t mFreeOffset = 0; // [mFreeoffset, mSize) is the area never been allocated
        uint32_t mSize = 0;

    };
}