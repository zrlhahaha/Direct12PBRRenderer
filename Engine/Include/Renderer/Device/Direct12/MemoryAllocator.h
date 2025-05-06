#pragma once
#include <d3d12.h>
#include "D3DUtils.h"

#include "Utils/Allocator.h"
#include "Fundation.h"
#include <variant>
#include <unordered_map>
#include <optional>

namespace MRenderer
{
    struct UploadBuffer 
    {
    public:
        UploadBuffer(ID3D12Resource* resource, void* mMapped, uint32 offset, uint32 size)
            :resource(resource), mapped(mMapped), offset(offset), size(size)
        {
        
        }

        void Upload(const void* data, size_t data_size);

    public:
        ID3D12Resource* resource;
        void* mapped;
        uint32 offset;
        uint32 size;
    };


    class UploadBufferPool 
    {
    protected:
        struct Page
        {
            ID3D12Resource* resource;
            void* mapped;
        };

        struct LargePageContainer
        {
            std::vector<Page> pages;
            size_t page_index;
            size_t page_size;
        };

    public:
        static constexpr size_t PageSize = 1024 * 1024; // 1MB

    public:
        UploadBufferPool() = default;
        ~UploadBufferPool();
        UploadBufferPool(const UploadBufferPool&) = delete;
        UploadBufferPool& operator=(const UploadBufferPool&) = delete;;

        UploadBuffer Allocate(ID3D12Device* device, uint32 size);
        void CleanUp();

    protected:
        UploadBuffer AllocateSmallBuffer(ID3D12Device* device, uint32 size);
        UploadBuffer AllocateLargeBuffer(ID3D12Device* device, uint32 size);

    protected:
        std::vector<Page> mPages;
        std::unordered_map<uint32, LargePageContainer> mLargePages;
        uint32 mPageIndex = 0;
        uint32 mOffset = 0;
    };


    class UploadBufferAllocator
    {
    public:
        UploadBufferAllocator(ID3D12Device* mDevice);
        UploadBufferAllocator(const UploadBufferAllocator&) = delete;
        UploadBufferAllocator& operator=(const UploadBufferAllocator&) = delete;

        UploadBuffer Allocate(uint32 size);
        void NextFrame();

    protected:
        UploadBufferPool mPools[FrameResourceCount];
        uint32 mFrameIndex;
        ID3D12Device* mDevice;
    };


    struct AllocationDesc
    {
        D3D12_RESOURCE_DESC resource_desc;
        D3D12_HEAP_TYPE heap_type;
        D3D12_RESOURCE_STATES initial_state;
        // optimized default value of resource for RT, depth stencil. it's ignored for other kind of resource
        D3D12_CLEAR_VALUE defalut_value;
    };


    enum EHeapUsage
    {
        // In order to support Tier 1 heaps, buffers, textures, and RT, DS textures need to be categorized into different heaps.
        // for more information, check the link below
        // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_resource_heap_tier
        EHeapUsage_Non_RT_DS_Textures = 0, // correspond to D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES
        EHeapUsage_Buffer = 1, // correspond to D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS
        EHeapTypeUsage_RT_DS_Textures = 2, // correspond to D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES
        EHeapUsage_TotalUsage = 3 // TotalUsage must be equal to the total number of other enums
    };


    enum EMemoryAllocationType
    {
        EMemoryAllocationType_Placed,
        EMemoryAllocationType_Commited,
    };

    namespace D3D12Memory
    {
        class MemoryAllocator;

        constexpr D3D12_HEAP_TYPE HeapType[] = { D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_TYPE_UPLOAD, D3D12_HEAP_TYPE_READBACK };
        constexpr uint32 HeapTypeTotal = static_cast<uint32>(std::size(HeapType));

        constexpr uint32 HeapCount = EHeapUsage_TotalUsage * HeapTypeTotal; // each heap usage has three kind of heap, default, upload and readback
        constexpr uint32 HeapSize = 64 * 1024 * 1024; // 64mb
        constexpr uint32 MaxAllocationSize = 2048 * 2048 * 32; // maximum allocation is the size of a 2048*2048 rgba texture
        constexpr uint32 MinAllocationSize = 256; // minimum allocation is the size of a samllest const buffer

        // max min size needs to be the power of 2
        static_assert((MaxAllocationSize& (MaxAllocationSize - 1)) == 0);
        static_assert((MinAllocationSize& (MinAllocationSize - 1)) == 0);

        using MetaAllocator = TLSFMeta<MinAllocationSize, FLS(MaxAllocationSize)>;
        using MetaAllocation = MetaAllocator::Allocation;

        struct MemoryAllocation
        {
            friend class HeapAllocator;
            friend class MemoryAllocator;
        private:
            union
            {
                struct PlacedAllocation
                {
                    ID3D12Resource* resource = nullptr;
                    MetaAllocation* meta_allocation = nullptr;
                    uint32 heap_index;
                } placed_allocation;

                struct CommitedAllocation
                {
                    ID3D12Resource* resource = nullptr;
                } commited_allocation;

                ID3D12Resource* resource = nullptr;
            } data;

            EMemoryAllocationType type;
            MemoryAllocator* source;

        public:
            ID3D12Resource* Resource() const
            {
                return data.resource;
            }

            MemoryAllocator* Allocator() const
            {
                return source;
            }
        };

        // This class uses a TLSF memory pool to manage a section of GPU memory.
        // The TLSFMeta is only for bookkeeping, The actual memory resides in the GPU memory that the ID3D12Heap represents.
        class MemoryAllocator
        {
        public:
            MemoryAllocator(ID3D12Device* device);
            ~MemoryAllocator();
            MemoryAllocator(const MemoryAllocator&) = delete;
            MemoryAllocator& operator=(const MemoryAllocator&) = delete;

            MemoryAllocation* Allocate(AllocationDesc desc, bool placed = true);
            void Free(MemoryAllocation* allocation);
            D3D12_RESOURCE_ALLOCATION_INFO QueryResourceSizeAndAlignment(D3D12_RESOURCE_DESC& desc);

        protected:
            MemoryAllocation* AllocateCommitedResouce(const AllocationDesc& desc);
            MemoryAllocation* AllocatePlacedResource(const AllocationDesc& desc, uint32 size, uint32 alignment);
            void FreeCommitedResource(MemoryAllocation* allocation);
            void FreePlacedResource(MemoryAllocation* allocation);

            inline static uint32 HeapIndex(EHeapUsage heap_usage, D3D12_HEAP_TYPE heap_type)
            {
                return heap_usage * HeapTypeTotal + heap_type - D3D12_HEAP_TYPE_DEFAULT;
            }

        protected:
            MetaAllocator mMeta[HeapCount];
            ComPtr<ID3D12Heap> mHeap[HeapCount];
            NestedObjectAllocator<MemoryAllocation> mAllocationAllocator;
            ID3D12Device* mDevice;
        };
    }

    using D3D12Memory::MemoryAllocation;
    using D3D12Memory::MemoryAllocator;
}


