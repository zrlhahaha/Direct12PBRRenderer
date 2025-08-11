#pragma once
#include "D3DUtils.h"
#include "Utils/Allocator.h"
#include "Fundation.h"

#include <d3d12.h>
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
        bool prefer_commited;
    };

    enum EMemoryAllocationType
    {

    };

    namespace D3D12Memory
    {
        class MemoryAllocator;

        // In order to support Tier 1 heaps, buffers, textures, and RT, DS textures need to be categorized into different heaps.
        // ref: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_resource_heap_tier
        constexpr D3D12_HEAP_FLAGS DeviceHeapFlags[] = { D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES, D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS};
        constexpr D3D12_HEAP_TYPE DeviceHeapTypes[] = { D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_TYPE_UPLOAD, D3D12_HEAP_TYPE_READBACK };

        constexpr uint32 DeviceHeapCount = static_cast<uint32>(std::size(DeviceHeapTypes) * std::size(DeviceHeapFlags)); // each heap usage has three kind of heap, default, upload and readback
        constexpr uint32 DeviceHeapPageSize = 64 * 1024 * 1024; // 64mb
        constexpr uint32 MaxAllocationSize = 2048 * 2048 * 32; // maximum allocation is the size of a 2048*2048 rgba texture
        constexpr uint32 MinAllocationSize = 256; // minimum allocation is the size of a samllest const buffer

        // max min size needs to be the power of 2
        static_assert((MaxAllocationSize& (MaxAllocationSize - 1)) == 0);
        static_assert((MinAllocationSize& (MinAllocationSize - 1)) == 0);

        using MetaAllocator = TLSFMeta<MinAllocationSize, FLS(MaxAllocationSize)>;
        using MetaAllocation = MetaAllocator::Allocation;

        struct PlacedAllocation
        {
            ID3D12Resource* Resource;
            MetaAllocation* MetaAllocation;
            HeapMemoryAllocator* Source;
            uint32 PageIndex;

            inline bool Valid() const { return Resource != nullptr; }
        };

        struct CommitedAllocation
        {
            ID3D12Resource* Resource = nullptr;
        };

        struct MemoryAllocation
        {
            friend class MemoryAllocator;
        private:
            std::variant<PlacedAllocation, CommitedAllocation> Allocation;
            MemoryAllocator* Source;

        public:
            inline ID3D12Resource* Resource()
            {
                Overload overloads =
                {
                    [](PlacedAllocation& allocation) {return allocation.Resource; },
                    [](CommitedAllocation& allocation) {return allocation.Resource; },
                };

                return std::visit(overloads, Allocation);
            }

            inline MemoryAllocator* Allocator() const
            {
                return Source;
            }
        };

        // This class uses a TLSF memory pool to manage a section of GPU memory.
        // The TLSFMeta is only for bookkeeping, The actual memory resides in the GPU memory that the ID3D12Heap represents.
        class HeapMemoryAllocator 
        {
        public:
            HeapMemoryAllocator(ID3D12Device*device, D3D12_HEAP_TYPE heap_type, D3D12_HEAP_FLAGS heap_flag);
            HeapMemoryAllocator(const HeapMemoryAllocator&) = delete;
            HeapMemoryAllocator& operator=(const HeapMemoryAllocator&) = delete;

            PlacedAllocation Allocate(const AllocationDesc& desc);
            
            // release the resource
            void Free(PlacedAllocation& allocation);

            // only release the MetaAllocation, allow other resource overlay with it
            // it requires that the previous resources and the resources allocated later not to be used simultaneously
            void AliasFree(PlacedAllocation& allocation);

        protected:
            D3D12_RESOURCE_ALLOCATION_INFO QueryResourceSizeAndAlignment(D3D12_RESOURCE_DESC desc);
            ComPtr<ID3D12Heap> CreateGPUHeap(D3D12_HEAP_TYPE heap_type, D3D12_HEAP_FLAGS heap_flag);
            std::tuple<int, MetaAllocation*> MetaAllocate(const AllocationDesc& desc, uint32 size, uint32 alignment);
            
        protected:
            static D3D12_HEAP_FLAGS GetResourceHeapFlag(const D3D12_RESOURCE_DESC& desc);

        protected:
            std::vector<MetaAllocator> mPagesMeta; // for memory pool management
            std::vector<ComPtr<ID3D12Heap>> mPages; // for actual GPU memory allocation
            ID3D12Device* mDevice;

            D3D12_HEAP_TYPE mHeapType;
            D3D12_HEAP_FLAGS mHeapFlag;
        };

        class MultiHeapMemoryAllocator
        {
        public:
            MultiHeapMemoryAllocator(ID3D12Device* device);
            MultiHeapMemoryAllocator(const MultiHeapMemoryAllocator&) = delete;
            MultiHeapMemoryAllocator& operator=(const MultiHeapMemoryAllocator&) = delete;

            PlacedAllocation Allocate(AllocationDesc desc);
            void Free(PlacedAllocation& allocation);
            void AliasFree(PlacedAllocation& allocation);

            inline uint32 MaxResourceSize() const { return DeviceHeapPageSize; }

        protected:
            static uint32 HeapIndex(D3D12_HEAP_TYPE heap_type, D3D12_HEAP_FLAGS heap_flag);

        protected:
            std::vector<HeapMemoryAllocator> mHeaps;
            ID3D12Device* mDevice;
        };

        class MemoryAllocator
        {
        public:
            static constexpr int InvalidPlacedHeapIndex = -1;

        public:
            MemoryAllocator(ID3D12Device* device);
            ~MemoryAllocator();
            MemoryAllocator(const MemoryAllocator&) = delete;
            MemoryAllocator& operator=(const MemoryAllocator&) = delete;

            MemoryAllocation* Allocate(AllocationDesc desc);
            void Free(MemoryAllocation*& allocation);

        protected:
            CommitedAllocation AllocateCommitedResouce(const AllocationDesc& desc);

        protected:
            MultiHeapMemoryAllocator mHeapAllocator;
            NestedObjectAllocator<MemoryAllocation> mAllocationAllocator;
            ID3D12Device* mDevice;
        };
    }

    using D3D12Memory::MemoryAllocation;
    using D3D12Memory::MemoryAllocator;
}


