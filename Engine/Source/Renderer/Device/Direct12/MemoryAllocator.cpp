#include "Renderer/Device/Direct12/MemoryAllocator.h"
#include "Fundation.h"
#include "Utils/Misc.h"

namespace MRenderer 
{
    void UploadBuffer::Upload(const void* data, size_t data_size)
    {
        ASSERT(resource && mapped && data_size <= size);
        memcpy(mapped, data, data_size);
    }

    UploadBufferPool::~UploadBufferPool()
    {
        for (auto& page: mPages)
        {
            page.resource->Release();
            page.resource = nullptr;
        }

        for (auto& container: mLargePages)
        {
            for (auto& page: container.second.pages)
            {
                page.resource->Release();
                page.resource = nullptr;
                page.mapped = nullptr;
            }
        }
    }

    UploadBuffer UploadBufferPool::Allocate(ID3D12Device* device, uint32 size)
    {
        if (size <= PageSize) 
        {
            return AllocateSmallBuffer(device, size);
        }
        else 
        {
            return AllocateLargeBuffer(device, size);
        }
    }

    void UploadBufferPool::CleanUp()
    {
        // release upload buffers that were not used in this frame
        // small pages
        size_t erase_begin = mOffset ? mPageIndex + 1 : mPageIndex;
        for (size_t i = erase_begin; i < mPages.size(); i++)
        {
            mPages[i].resource->Release();
            mPages[i].resource = nullptr;
            mPages[i].mapped = nullptr;
        }

        if (erase_begin < mPages.size()) 
        {
            mPages.erase(mPages.begin() + erase_begin, mPages.end());
        }

        mOffset = 0;
        mPageIndex = 0;

        // large pages
        for (auto it = mLargePages.begin(); it != mLargePages.end();) 
        {
            LargePageContainer& container = it->second;
            auto& pages = container.pages;

            // remove large page if it's not used in this frame
            for (size_t i = container.page_index; i < pages.size(); i++)
            {
                pages[i].resource->Release();
                pages[i].resource = nullptr;
                pages[i].mapped = nullptr;
            }
            pages.erase(pages.begin() + container.page_index, pages.end());
            container.page_index = 0;

            // remove empty bucket
            if (pages.empty()) 
            {
                it = mLargePages.erase(it);
            }
            else 
            {
                it++;
            }
        }
    }

    // this is for small gpu resources, they will use a suballocation of a buffer as their upload buffer
    UploadBuffer UploadBufferPool::AllocateSmallBuffer(ID3D12Device* device, uint32 size)
    {
        ASSERT(size <= PageSize);

        // small buffer
        if (mPageIndex < mPages.size() && mOffset + size > PageSize) 
        {
            mPageIndex++;
            mOffset = 0;
        }

        if (mPageIndex < mPages.size())
        {
            void* mapped = static_cast<char*>(mPages[mPageIndex].mapped) + mOffset;
            UploadBuffer allocation(mPages[mPageIndex].resource, mapped, mOffset, size);
            mOffset += size;
            return allocation;
        }
        else if (mPageIndex == mPages.size())
        {
            ID3D12Resource* res;
            auto heap_property = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            auto resource_desc = CD3DX12_RESOURCE_DESC::Buffer(PageSize);

            ThrowIfFailed(device->CreateCommittedResource(
                &heap_property,
                D3D12_HEAP_FLAG_NONE,
                &resource_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&res)
            ));

            void* mapped = nullptr;
            res->Map(0, nullptr, &mapped);
            mPages.emplace_back(res, mapped);

            mOffset += size;
            return UploadBuffer(res, mapped, 0, size);
        }
        else
        {
            UNEXPECTED("");
            return UploadBuffer(nullptr, nullptr, 0, 0);
        }
    }

    // this is for resources such as detailed mesh or texture, whose's size is above UploadBufferAllocator::PageSize
    UploadBuffer UploadBufferPool::AllocateLargeBuffer(ID3D12Device* device, uint32 size)
    {
        ASSERT(size > PageSize);
        auto& container = mLargePages[size];
        if (container.page_index < container.pages.size())
        {
            auto& page = container.pages[container.page_index];
            container.page_index++;
            return UploadBuffer(page.resource, page.mapped, 0, size);
        }
        else if(container.page_index == container.pages.size())
        {
            ID3D12Resource* resource;
            auto heap_property = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            auto resource_desc = CD3DX12_RESOURCE_DESC::Buffer(size);

            ThrowIfFailed(device->CreateCommittedResource(
                &heap_property,
                D3D12_HEAP_FLAG_NONE,
                &resource_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&resource)
            ));

            void* mapped = nullptr;
            resource->Map(0, nullptr, &mapped);

            container.pages.push_back({ resource, mapped});
            container.page_index++;
            return UploadBuffer(resource, mapped, 0, size);
        }
        else 
        {
            UNEXPECTED("Invalid Page Index");
            return UploadBuffer(nullptr, nullptr, 0, 0);
        }
    }

    UploadBufferAllocator::UploadBufferAllocator(ID3D12Device* device)
        :mFrameIndex(0), mDevice(device)
    {
    }

    UploadBuffer UploadBufferAllocator::Allocate(uint32 size)
    {
        return mPools[mFrameIndex].Allocate(mDevice, size);
    }

    void UploadBufferAllocator::NextFrame()
    {
        mFrameIndex = (mFrameIndex + 1) % FrameResourceCount;
        mPools[mFrameIndex].CleanUp();
    }

    namespace D3D12Memory
    {
        HeapMemoryAllocator::HeapMemoryAllocator(ID3D12Device* device, D3D12_HEAP_TYPE heap_type, D3D12_HEAP_FLAGS heap_flag)
            :mDevice(device), mHeapFlag(heap_flag), mHeapType(heap_type)
        {
        }

        PlacedAllocation HeapMemoryAllocator::Allocate(const AllocationDesc& desc)
        {
            ASSERT(desc.heap_type == mHeapType);
            ASSERT(GetResourceHeapFlag(desc.resource_desc) == mHeapFlag);

            D3D12_RESOURCE_ALLOCATION_INFO info = QueryResourceSizeAndAlignment(desc.resource_desc);

            auto[heap_index, meta_allocation] = MetaAllocate(desc,static_cast<uint32>(info.SizeInBytes), static_cast<uint32>(info.Alignment));
            if(heap_index == -1)
            {
                return {};
            }

            bool use_default_value = desc.resource_desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

            ID3D12Resource* resource;
            ThrowIfFailed
            (
                mDevice->CreatePlacedResource(
                    mPages[heap_index].Get(),
                    meta_allocation->Offset,
                    &desc.resource_desc,
                    desc.initial_state,
                    use_default_value ? &desc.defalut_value : nullptr,
                    IID_PPV_ARGS(&resource)
                )
            );

            PlacedAllocation ret = {};
            ret.PageIndex = heap_index;
            ret.Resource = resource;
            ret.MetaAllocation = meta_allocation;
            ret.Source = this;

            return ret;
        }

        void HeapMemoryAllocator::Free(PlacedAllocation& allocation)
        {
            ASSERT(allocation.Resource && allocation.PageIndex && allocation.Source == this);

            mPagesMeta[allocation.PageIndex].Free(allocation.MetaAllocation);
            allocation.Resource->Release();

            allocation = {};
        }

        void HeapMemoryAllocator::ReleasePlacedMemory(PlacedAllocation& allocation)
        {
            ASSERT(allocation.Resource && allocation.PageIndex && allocation.Source == this);

            mPagesMeta[allocation.PageIndex].Free(allocation.MetaAllocation);
        }

        ComPtr<ID3D12Heap> HeapMemoryAllocator::CreateGPUHeap(D3D12_HEAP_TYPE heap_type, D3D12_HEAP_FLAGS heap_flag)
        {
            D3D12_HEAP_DESC heap_desc;
            heap_desc.SizeInBytes = DeviceHeapPageSize;

            // heap alignment will always be D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT (i.e 64KB)
            // for MSAA textures which requires 4MB alignment, they will be allocated as commited resources
            heap_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
            heap_desc.Properties = D3D12_HEAP_PROPERTIES{ .Type = heap_type };
            heap_desc.Flags = heap_flag;

            ID3D12Heap* heap;
            ThrowIfFailed(mDevice->CreateHeap(&heap_desc, IID_PPV_ARGS(&heap)));

            return heap;
        }

        D3D12_RESOURCE_ALLOCATION_INFO HeapMemoryAllocator::QueryResourceSizeAndAlignment(D3D12_RESOURCE_DESC desc)
        {
            // refered from: https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator
            // AllocatorPimpl::GetResourceAllocationInfo

            //Buffers have the same size on all adapters
            // which is merely the smallest multiple of 64KB that's greater or equal toD3D12_RESOURCE_DESC::Width.
            if (desc.Alignment == 0 && desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
            {
                return { AlignUp(static_cast<uint32>(desc.Width), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT };
            }

            if (desc.Alignment == 0 && desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && desc.Flags)
                //(desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) == D3D12_RESOURCE_FLAG_NONE)
            {
                //The algorithm here is based on Microsoft sample: "Small Resources Sample"
                // https://github.com/microsoft/DirectX-Graphics-Samples/tree/master/Samples/Desktop/D3D12SmallResources
                uint64 alignment = desc.Alignment = desc.SampleDesc.Count > 1 ? D3D12_SMALL_MSAA_RESOURCE_PLACEMENT_ALIGNMENT : D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT;
                D3D12_RESOURCE_ALLOCATION_INFO info = mDevice->GetResourceAllocationInfo(0, 1, &desc);

                // Check if alignment requested has been granted.
                if (info.Alignment == alignment)
                {
                    return info;
                }
            }

            desc.Alignment = 0;
            return mDevice->GetResourceAllocationInfo(0, 1, &desc);
        }

        std::tuple<int, MetaAllocation*> HeapMemoryAllocator::MetaAllocate(const AllocationDesc& desc, uint32 size, uint32 alignment)
        {
            if (size > DeviceHeapPageSize) 
            {
                return { -1, nullptr };
            }

            // find out where to allocate
            for (uint32 i = 0; i < mPagesMeta.size(); i++)
            {
                MetaAllocation* allocation_info = mPagesMeta[i].Allocate(size, alignment);
                if (allocation_info)
                {
                    return { i, allocation_info };
                }
            }

            mPages.emplace_back(CreateGPUHeap(mHeapType, mHeapFlag));
            mPagesMeta.emplace_back(DeviceHeapPageSize);

            MetaAllocation* allocation_info = mPagesMeta.back().Allocate(size, alignment);
            ASSERT(allocation_info);

            return {static_cast<uint32>(mPagesMeta.size() - 1), allocation_info};
        }

        D3D12_HEAP_FLAGS HeapMemoryAllocator::GetResourceHeapFlag(const D3D12_RESOURCE_DESC& desc)
        {
            // allocated as placed resource
            D3D12_HEAP_FLAGS heap_flag;
            if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
            {
                heap_flag = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
            }
            else if (desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
            {
                heap_flag = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
            }
            else
            {
                heap_flag = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
            }
            return heap_flag;
        }

        MultiHeapMemoryAllocator::MultiHeapMemoryAllocator(ID3D12Device* device)
            :mDevice(device)
        {
            for (uint32 i = 0; i < std::size(DeviceHeapTypes); i++)
            {
                for (uint32 j = 0; j < std::size(DeviceHeapFlags); j++)
                {
                    D3D12_HEAP_TYPE heap_type = DeviceHeapTypes[i];
                    D3D12_HEAP_FLAGS heap_flag = DeviceHeapFlags[j];

                    mHeaps.emplace_back(std::make_unique<HeapMemoryAllocator>(device, heap_type, heap_flag));
                }
            }
        }

        PlacedAllocation MultiHeapMemoryAllocator::Allocate(AllocationDesc desc)
        {
            // allocated as placed resource
            D3D12_HEAP_FLAGS heap_flag;
            if (desc.resource_desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
            {
                heap_flag = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
            }
            else if (desc.resource_desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
            {
                heap_flag = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
            }
            else
            {
                heap_flag = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
            }

            // find out where to allocate
            uint32 heap_index = HeapIndex(desc.heap_type, heap_flag);
            PlacedAllocation ret = mHeaps[heap_index]->Allocate(desc);

            return ret;
        }

        void MultiHeapMemoryAllocator::Free(PlacedAllocation& allocation)
        {
            ASSERT(allocation.Resource && allocation.Source);
            allocation.Source->Free(allocation);
        }

        void MultiHeapMemoryAllocator::ReleasePlacedMemory(PlacedAllocation& allocation)
        {
            ASSERT(allocation.Resource && allocation.Source);
            allocation.Source->ReleasePlacedMemory(allocation);
        }

        void MultiHeapMemoryAllocator::ResetPlacedMemory()
        {
            for (auto& heap : mHeaps)
            {
                heap->AliasReset();
            }
        }

        inline uint32 MultiHeapMemoryAllocator::HeapIndex(D3D12_HEAP_TYPE heap_type, D3D12_HEAP_FLAGS heap_flag)
        {
            int b = (heap_flag >> 6) - 1; // DeviceHeapFlags = [01000100, 10000100, 11000000], we can get the index by shift D3D12_HEAP_FLAGS to the right by 6 bits and minus 1
            int a = heap_type - DeviceHeapTypes[0];

            int ret = static_cast<int>(a * std::size(DeviceHeapTypes) + b);
            ASSERT(ret < DeviceHeapCount && ret >= 0);

            return ret;
        }
}
}