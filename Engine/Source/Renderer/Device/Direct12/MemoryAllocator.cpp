#include "Renderer/Device/Direct12/MemoryAllocator.h"
#include "Fundation.h"
#include "Utils/Misc.h"

namespace MRenderer 
{
    void UploadBuffer::Upload(const void* data, uint32 data_size)
    {
        ASSERT(Resource && Mapped && data_size <= Size);
        void* start = reinterpret_cast<char*>(Mapped) + Offset;
        memcpy(start, data, data_size);
    }

    // ref: https://learn.microsoft.com/en-us/windows/win32/direct3d12/upload-and-readback-of-texture-data
    // to see the alignment for different resource, check out reference link.
    // generally speaking, 256 for cbuffer, 512 for texture
    UploadBuffer UploadBufferPool::Allocate(ID3D12Device* device, uint32 size, uint32 alignment)
    {
        if (size <= PageSize) 
        {
            return AllocateSmallBuffer(device, size, alignment);
        }
        else 
        {
            return AllocateLargeBuffer(device, size);
        }
    }

    void UploadBufferPool::CleanUp()
    {
        // release pages that were not used in this frame
        // small pages
        uint32 erase_begin = mOffset ? mPageIndex + 1 : mPageIndex; // release current page if it's untouched
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
            auto& pages = container.Pages;

            pages.erase(pages.begin() + container.PageIndex, pages.end());
            container.PageIndex = 0;

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

    // ref: https://learn.microsoft.com/en-us/windows/win32/direct3d12/uploading-resources
    // this is for small gpu resources, the upload buffer will be a suballocator from a larger buffer
    UploadBuffer UploadBufferPool::AllocateSmallBuffer(ID3D12Device* device, uint32 size, uint32 alignment)
    {
        ASSERT(size <= PageSize);
        //D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT
        uint32 require_size = AlignUp(mOffset, alignment) - mOffset + size;

        // advance page to the next one
        if (mOffset + require_size > PageSize) 
        {
            mPageIndex++;
            mOffset = 0;
        }

        if (mPageIndex < mPages.size())
        {
            UploadBuffer allocation(mPages[mPageIndex].Resource.Get(), mPages[mPageIndex].Mapped, AlignUp(mOffset, alignment), size);
            mOffset += require_size;
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
            ThrowIfFailed(res->Map(0, nullptr, &mapped));
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
        if (container.PageIndex < container.Pages.size())
        {
            auto& page = container.Pages[container.PageIndex];
            container.PageIndex++;
            return UploadBuffer(page.Resource.Get(), page.Mapped, 0, size);
        }
        else if(container.PageIndex == container.Pages.size())
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

            container.Pages.push_back({ resource, mapped});
            container.PageIndex++;
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

    UploadBuffer UploadBufferAllocator::Allocate(uint32 size, uint32 alignment)
    {
        return mPools[mFrameIndex].Allocate(mDevice, size, alignment);
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
            ASSERT(desc.HeapType == mHeapType);
            ASSERT(GetResourceHeapFlag(desc.ResourceDesc) == mHeapFlag);

            D3D12_RESOURCE_ALLOCATION_INFO info = QueryResourceSizeAndAlignment(desc.ResourceDesc);

            auto[heap_index, meta_allocation] = MetaAllocate(desc,static_cast<uint32>(info.SizeInBytes), static_cast<uint32>(info.Alignment));
            if(heap_index == -1)
            {
                return {};
            }

            bool use_default_value = desc.ResourceDesc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

            ID3D12Resource* resource;
            ThrowIfFailed
            (
                mDevice->CreatePlacedResource(
                    mPages[heap_index].Get(),
                    meta_allocation->Offset,
                    &desc.ResourceDesc,
                    desc.InitialState,
                    use_default_value ? &desc.DefaultValue : nullptr,
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
            ASSERT(allocation.Resource && allocation.Source == this);

            mPagesMeta[allocation.PageIndex].Free(allocation.MetaAllocation);
            allocation.Resource->Release();

            allocation = {};
        }

        void HeapMemoryAllocator::ReleasePlacedMemory(PlacedAllocation& allocation)
        {
            ASSERT(allocation.Resource && allocation.Source == this);

            mPagesMeta[allocation.PageIndex].Free(allocation.MetaAllocation);
        }

        ComPtr<ID3D12Heap> HeapMemoryAllocator::CreateGPUHeap(D3D12_HEAP_TYPE heap_type, D3D12_HEAP_FLAGS heap_flag)
        {
            D3D12_HEAP_DESC heap_desc;
            heap_desc.SizeInBytes = DeviceHeapPageSize;

            // heap alignment will always be D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT (i.e 64KB)
            // for MSAA textures which requires 4MB alignment, they will be allocated as commited resources
            heap_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
            heap_desc.Properties = CD3DX12_HEAP_PROPERTIES(heap_type);
            heap_desc.Flags = heap_flag;

            ID3D12Heap* heap;
            ThrowIfFailed(mDevice->CreateHeap(&heap_desc, IID_PPV_ARGS(&heap)));

            return heap;
        }

        // get gpu resource size and alignment for the given resource description
        D3D12_RESOURCE_ALLOCATION_INFO HeapMemoryAllocator::QueryResourceSizeAndAlignment(D3D12_RESOURCE_DESC desc)
        {
            // ref: https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator AllocatorPimpl::GetResourceAllocationInfo
            //      https://github.com/microsoft/DirectX-Graphics-Samples/tree/master/Samples/Desktop/D3D12SmallResources
            //      https://asawicki.info/news_1726_secrets_of_direct3d_12_resource_alignment

            // most of dx12 resources are allocated by 64kb alignment or 4mb alignment, some certain resource with mip 0 size less than 64kb can be allocated by 4kb alignment.
            // this is what this function is trying to do, check if this resource can be 4kb aligned

            // Buffers have a guaranteed 64KB alignment requirement across all adapters
            // the size of a buffer should be the smallest multiple of 64KB that¡¯s greater than D3D12_RESOURCE_DESC::Width
            if (desc.Alignment == 0 && desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
            {
                return { AlignUp(static_cast<uint32>(desc.Width), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT };
            }

            // render target and depth stencil can't be 4kb aligned
            if (desc.Alignment == 0 && desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && 
                ((desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) == D3D12_RESOURCE_FLAG_NONE))
            {
                uint64 alignment = desc.Alignment = desc.SampleDesc.Count > 1 ? D3D12_SMALL_MSAA_RESOURCE_PLACEMENT_ALIGNMENT : D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT;
                D3D12_RESOURCE_ALLOCATION_INFO info = mDevice->GetResourceAllocationInfo(0, 1, &desc);

                // Check if alignment requested has been granted.
                if (info.Alignment == alignment)
                {
                    return info;
                }
            }

            // request declined, use dx12 suggested alignment
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
            if (desc.ResourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
            {
                heap_flag = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
            }
            else if (desc.ResourceDesc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
            {
                heap_flag = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
            }
            else
            {
                heap_flag = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
            }

            // find out where to allocate
            uint32 heap_index = HeapIndex(desc.HeapType, heap_flag);
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
            // DeviceHeapFlags = { D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES : 01000100, D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES : 10000100, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS : 11000000}
            // we can get the index by shift D3D12_HEAP_FLAGS to the right by 6 bits and minus 1
            int b = (heap_flag >> 6) - 1;
            int a = heap_type - DeviceHeapTypes[0];

            int ret = static_cast<int>(a * std::size(DeviceHeapFlags) + b);
            ASSERT(ret < DeviceHeapCount && ret >= 0);

            return ret;
        }
}
}