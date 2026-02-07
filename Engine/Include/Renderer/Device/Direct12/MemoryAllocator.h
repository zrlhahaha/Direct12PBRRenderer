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
    // this is a transient object only valid during the frame it is allocated at
    struct UploadBuffer 
    {
    public:
        UploadBuffer(ID3D12Resource* resource, void* mMapped, uint32 offset, uint32 size)
            :Resource(resource), Mapped(mMapped), Offset(offset), Size(size)
        {
        }

        void Upload(const void* data, uint32 data_size);

        // transient object, forbid copy and move operation
        UploadBuffer(const UploadBuffer&) = delete;
        UploadBuffer(UploadBuffer&&) = default;
        UploadBuffer& operator=(UploadBuffer&&) = default;
        UploadBuffer& operator=(const UploadBuffer&) = delete;

    public:
        ID3D12Resource* Resource;
        void* Mapped;
        uint32 Offset;
        uint32 Size;
    };


    class UploadBufferPool 
    {
    protected:
        struct Page
        {
            ComPtr<ID3D12Resource> Resource;
            void* Mapped;
        };

        struct LargePageContainer
        {
            std::vector<Page> Pages;
            uint32 PageIndex;
        };

    public:
        static constexpr uint32 PageSize = 8 * 1024 * 1024; // 8MB

    public:
        UploadBufferPool() = default;
        ~UploadBufferPool() = default;
        UploadBufferPool(const UploadBufferPool&) = delete;
        UploadBufferPool& operator=(const UploadBufferPool&) = delete;;

        UploadBuffer Allocate(ID3D12Device* device, uint32 size, uint32 alignment);
        void CleanUp();

    protected:
        UploadBuffer AllocateSmallBuffer(ID3D12Device* device, uint32 size, uint32 alignment);
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

        UploadBuffer Allocate(uint32 size, uint32 alignment);
        void NextFrame();

    protected:
        UploadBufferPool mPools[FrameResourceCount];
        uint32 mFrameIndex;
        ID3D12Device* mDevice;
    };


    struct AllocationDesc
    {
        D3D12_RESOURCE_DESC ResourceDesc;
        D3D12_HEAP_TYPE HeapType;
        D3D12_RESOURCE_STATES InitialState;
        // optimized default value of resource for RT, depth stencil. it's ignored for other kind of resource
        D3D12_CLEAR_VALUE DefaultValue;
        bool PreferCommited;
    };

    namespace D3D12Memory
    {
        // In order to support Tier 1 heaps, buffers, textures, and RT, DS textures need to be categorized into different heaps.
        // ref: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_resource_heap_tier
        constexpr D3D12_HEAP_FLAGS DeviceHeapFlags[] = { D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES, D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS };
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

        class HeapMemoryAllocator;
        class ID3D12MemoryAllocator;

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
            friend class D3D12TransientMemoryAllocator;
            friend class D3D12MemoryAllocator;

        private:
            std::variant<PlacedAllocation, CommitedAllocation> Allocation;
            ID3D12MemoryAllocator* Source;

        public:
            ID3D12Resource* Resource()
            {
                Overload overloads =
                {
                    [](PlacedAllocation& allocation) {return allocation.Resource; },
                    [](CommitedAllocation& allocation) {return allocation.Resource; },
                };

                return std::visit(overloads, Allocation);
            }

            inline ID3D12MemoryAllocator* Allocator() { return Source; }
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
            void ReleasePlacedMemory(PlacedAllocation& allocation);
            void AliasReset()
            {
                for (auto& meta : mPagesMeta) 
                {
                    meta.Reset();
                }
            }

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
            void ReleasePlacedMemory(PlacedAllocation& allocation);
            void ResetPlacedMemory();
            inline ID3D12Device* Device() { return mDevice; }

            inline uint32 MaxResourceSize() const { return DeviceHeapPageSize; }

        protected:
            static uint32 HeapIndex(D3D12_HEAP_TYPE heap_type, D3D12_HEAP_FLAGS heap_flag);

        protected:
            std::vector<std::unique_ptr<HeapMemoryAllocator>> mHeaps;
            ID3D12Device* mDevice;
        };

        //d3d12 resource allocator, resource could be allocaated as commited resource or placed resource, depends on @AllocationPolicy.

        class ID3D12MemoryAllocator 
        {
        public:
            virtual ~ID3D12MemoryAllocator() = default;

            virtual MemoryAllocation* Allocate(AllocationDesc desc) = 0;
            virtual void Free(MemoryAllocation*& allocation) = 0;
        };

        class ID3D12TransientMemoryAllocator 
        {
        public:
            virtual void ReleasePlacedMemory(MemoryAllocation* allocation) = 0;
            virtual void ResetPlacedMemory() = 0;
        };

        class D3D12MemoryAllocator : public ID3D12MemoryAllocator
        {
            static constexpr int InvalidPlacedHeapIndex = -1;

        public:
            D3D12MemoryAllocator(ID3D12Device* device)
                : mDevice(device), mHeapAllocator(device)
            {
            }
            ~D3D12MemoryAllocator() = default;

            D3D12MemoryAllocator(const D3D12MemoryAllocator&) = delete;
            D3D12MemoryAllocator& operator=(const D3D12MemoryAllocator&) = delete;

            MemoryAllocation* Allocate(AllocationDesc desc) override
            {
                MemoryAllocation* ret = mAllocationAllocator.Allocate();
                if (desc.PreferCommited)
                {
                    ret->Allocation = AllocateCommitedResouce(mDevice, desc);
                }
                else
                {
                    PlacedAllocation placed_allocation = mHeapAllocator.Allocate(desc);
                    if (placed_allocation.Valid())
                    {
                        ret->Allocation = placed_allocation;
                    }
                    else
                    {
                        ret->Allocation = AllocateCommitedResouce(mHeapAllocator.Device(), desc);
                    }
                }

                ret->Source = this;
                return ret;
            }

            void Free(MemoryAllocation*& allocation) override
            {
                Overload overloads
                {
                    [&](CommitedAllocation& allocation) {allocation.Resource->Release(); },
                    [&](PlacedAllocation& allocation) {mHeapAllocator.Free(allocation); },
                };

                std::visit(overloads, allocation->Allocation);
                mAllocationAllocator.Free(allocation);
            }

        protected:
            CommitedAllocation AllocateCommitedResouce(ID3D12Device* device, const AllocationDesc& desc) 
            {
                ID3D12Resource* res;
                auto heap_property = CD3DX12_HEAP_PROPERTIES(desc.HeapType);

                bool use_default_value = desc.ResourceDesc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

                ThrowIfFailed(device->CreateCommittedResource(
                    &heap_property,
                    D3D12_HEAP_FLAG_NONE,
                    &desc.ResourceDesc,
                    desc.InitialState,
                    use_default_value ? &desc.DefaultValue : nullptr,
                    IID_PPV_ARGS(&res)
                ));

                CommitedAllocation allocation = {};
                allocation.Resource = res;

                return allocation;
            }

        protected:
            MultiHeapMemoryAllocator mHeapAllocator;
            NestedObjectAllocator<MemoryAllocation> mAllocationAllocator;
            ID3D12Device* mDevice;
        };


        // It contains all transient resources. They are allocated as placed resources and share the same memory if their lifetimes do not overlap.
        class D3D12TransientMemoryAllocator : public ID3D12MemoryAllocator, public ID3D12TransientMemoryAllocator
        {
        public:
            D3D12TransientMemoryAllocator(ID3D12Device* device)
                :mHeapAllocator(device)
            {
            }
            ~D3D12TransientMemoryAllocator() = default;

            D3D12TransientMemoryAllocator(const D3D12TransientMemoryAllocator&) = delete;
            D3D12TransientMemoryAllocator& operator=(const D3D12TransientMemoryAllocator&) = delete;

            MemoryAllocation* Allocate(AllocationDesc desc) override 
            {
                PlacedAllocation placed_allocation = mHeapAllocator.Allocate(desc);
                ASSERT(placed_allocation.Valid());

                MemoryAllocation* ret = mAllocationAllocator.Allocate();
                ret->Source = this;
                ret->Allocation = placed_allocation;

                return ret;
            }
            
            void Free(MemoryAllocation*& allocation) override 
            {
                Overload overloads
                {
                    [&](CommitedAllocation& allocation) {ASSERT(false); },
                    [&](PlacedAllocation& allocation) {mHeapAllocator.Free(allocation); },
                };

                std::visit(overloads, allocation->Allocation);
                mAllocationAllocator.Free(allocation);
            }

            void ReleasePlacedMemory(MemoryAllocation* allocation) override
            {
                Overload overloads
                {
                    [&](CommitedAllocation& allocation) {ASSERT(false); },
                    [&](PlacedAllocation& allocation) {mHeapAllocator.ReleasePlacedMemory(allocation); },
                };

                std::visit(overloads, allocation->Allocation);
            }

            void ResetPlacedMemory() override
            {
                mHeapAllocator.ResetPlacedMemory();
            }

        protected:
            MultiHeapMemoryAllocator mHeapAllocator;
            NestedObjectAllocator<MemoryAllocation> mAllocationAllocator;
        };
    }

    using D3D12Memory::MemoryAllocation;
}


