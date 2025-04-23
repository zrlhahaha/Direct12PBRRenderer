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

    UploadBuffer UploadBufferPool::Allocate(ID3D12Device* device, size_t size)
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
        return;

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
    UploadBuffer UploadBufferPool::AllocateSmallBuffer(ID3D12Device* device, size_t size)
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
    UploadBuffer UploadBufferPool::AllocateLargeBuffer(ID3D12Device* device, size_t size)
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

    UploadBuffer UploadBufferAllocator::Allocate(size_t size)
    {
        return mPools[mFrameIndex].Allocate(mDevice, size);
    }

    void UploadBufferAllocator::NextFrame()
    {
        return;
        mFrameIndex = (mFrameIndex + 1) % FrameResourceCount;
        mPools[mFrameIndex].CleanUp();
    }


    MemoryAllocator::MemoryAllocator(ID3D12Device* device)
        :mMeta{
            MetaAllocator(HeapSize) ,MetaAllocator(HeapSize) ,MetaAllocator(HeapSize),
            MetaAllocator(HeapSize), MetaAllocator(HeapSize) ,MetaAllocator(HeapSize),
            MetaAllocator(HeapSize), MetaAllocator(HeapSize) ,MetaAllocator(HeapSize)
        }, mDevice(device)
    {
        for (size_t usage_index = 0; usage_index < EHeapUsage_TotalUsage; usage_index++) 
        {
            for (size_t type_index = 0; type_index < HeapTypeTotal; type_index++)
            {
                EHeapUsage usage = static_cast<EHeapUsage>(usage_index);
                D3D12_HEAP_DESC heap_desc;
                heap_desc.SizeInBytes = HeapSize;

                // heap alignment will always be D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT (i.e 64KB)
                // for MSAA textures which requires 4MB alignment, they will be allocated as commited resources
                heap_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

                heap_desc.Properties = D3D12_HEAP_PROPERTIES{ .Type = HeapType[type_index]};

                // heap_desc
                if (usage == EHeapUsage_Non_RT_DS_Textures)
                    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
                else if (usage == EHeapTypeUsage_RT_DS_Textures)
                    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
                else if (usage == EHeapUsage_Buffer)
                    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
                else
                    ASSERT(false || "Unexpect");

                ThrowIfFailed(
                    mDevice->CreateHeap(
                        &heap_desc,
                        IID_PPV_ARGS(&mHeap[HeapIndex(static_cast<EHeapUsage>(usage_index), HeapType[type_index])])
                    )
                );
            }
        }
    }
    
    MemoryAllocator::~MemoryAllocator()
    {
    }


    MemoryAllocation* MemoryAllocator::Allocate(AllocationDesc desc, bool placed)
    {
        if(!placed)
        {
            return AllocateCommitedResouce(desc);
        }
        else 
        {
            D3D12_RESOURCE_ALLOCATION_INFO info = QueryResourceSizeAndAlignment(desc.resource_desc);
            MemoryAllocation* ret = AllocatePlacedResource(desc, info.SizeInBytes, info.Alignment);

            // fallback to commited resource
            if (!ret)
            {
                ret = AllocateCommitedResouce(desc);
            }

            return ret;
        }
    }

    void MemoryAllocator::Free(MemoryAllocation* allocation)
    {
        if (allocation->type == EMemoryAllocationType_Commited) 
        {
            FreeCommitedResource(allocation);
        }
        else if(allocation->type == EMemoryAllocationType_Placed)
        {
            FreePlacedResource(allocation);
        }
        else 
        {
            UNEXPECTED("Allocation Object Is Contaminated");
        }
    }

    D3D12_RESOURCE_ALLOCATION_INFO MemoryAllocator::QueryResourceSizeAndAlignment(D3D12_RESOURCE_DESC& desc)
    {
        // refered from: https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator
        // AllocatorPimpl::GetResourceAllocationInfo

        //Buffers have the same size on all adapters
        // which is merely the smallest multiple of 64KB that's greater or equal toD3D12_RESOURCE_DESC::Width.
        if (desc.Alignment == 0 && desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        {
            return {AlignUp(desc.Width, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT };
        }

        if (desc.Alignment == 0 && desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && desc.Flags)
            //(desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) == D3D12_RESOURCE_FLAG_NONE)
        {
            //The algorithm here is based on Microsoft sample: "Small Resources Sample"
            // https://github.com/microsoft/DirectX-Graphics-Samples/tree/master/Samples/Desktop/D3D12SmallResources
            uint32 alignment = desc.Alignment = desc.SampleDesc.Count > 1 ? D3D12_SMALL_MSAA_RESOURCE_PLACEMENT_ALIGNMENT : D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT;
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

    MemoryAllocation* MemoryAllocator::AllocateCommitedResouce(const AllocationDesc& desc)
    {
        ID3D12Resource* res;
        auto heap_property = CD3DX12_HEAP_PROPERTIES(desc.heap_type);

        bool use_default_value = desc.resource_desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

        ThrowIfFailed(mDevice->CreateCommittedResource(
            &heap_property,
            D3D12_HEAP_FLAG_NONE,
            &desc.resource_desc,
            desc.initial_state,
            use_default_value ? &desc.defalut_value : nullptr,
            IID_PPV_ARGS(&res)
        ));

        MemoryAllocation* allocation = mAllocationAllocator.Allocate();
        auto& commited_allocation = allocation->data.commited_allocation;

        commited_allocation.resource = res;
        allocation->type = EMemoryAllocationType_Commited;
        allocation->source = this;

        return allocation;
    }

    MemoryAllocation* MemoryAllocator::AllocatePlacedResource(const AllocationDesc& desc, size_t size, size_t alignment)
    {
        // allocated as placed resource
        EHeapUsage heap_usage;
        if (desc.resource_desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        {
            heap_usage = EHeapUsage_Buffer;
        }
        else if (desc.resource_desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
        {
            heap_usage = EHeapTypeUsage_RT_DS_Textures;
        }
        else
        {
            heap_usage = EHeapUsage_Non_RT_DS_Textures;
        }

        uint32 heap_index = HeapIndex(heap_usage, desc.heap_type);
        auto meta_allocation = mMeta[heap_index].Allocate(size, alignment);
        if (!meta_allocation)
        {
            // out of memory or the size is too large
            return nullptr;
        }

        bool use_default_value = desc.resource_desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

        ID3D12Resource* resource;
        ThrowIfFailed(mDevice->CreatePlacedResource(mHeap[heap_index].Get(), meta_allocation->offset, &desc.resource_desc,
            desc.initial_state, use_default_value ? &desc.defalut_value : nullptr, IID_PPV_ARGS(&resource)));

        MemoryAllocation* allocation = mAllocationAllocator.Allocate();

        allocation->type = EMemoryAllocationType_Placed;
        allocation->source = this;
        
        auto& placed_allocation = allocation->data.placed_allocation;
        placed_allocation.heap_index = heap_index;
        placed_allocation.resource = resource;
        placed_allocation.meta_allocation = meta_allocation;

        return allocation;
    }

    void MemoryAllocator::FreeCommitedResource(MemoryAllocation* allocation)
    {
        ASSERT(allocation->source == this && allocation->type == EMemoryAllocationType_Commited && allocation->Resource());

        auto& commited_allocation = allocation->data.commited_allocation;
        commited_allocation.resource->Release();
        commited_allocation.resource = nullptr;
        allocation->source = nullptr;

        mAllocationAllocator.Free(allocation);
    }

    void MemoryAllocator::FreePlacedResource(MemoryAllocation* allocation)
    {
        auto& placed_allocation = allocation->data.placed_allocation;
        ASSERT(allocation->source == this && allocation->type == EMemoryAllocationType_Placed && allocation->Resource() && placed_allocation.meta_allocation);

        mMeta[placed_allocation.heap_index].Free(placed_allocation.meta_allocation);
        placed_allocation.resource->Release();

        placed_allocation.meta_allocation = nullptr;
        placed_allocation.resource = nullptr;
        allocation->source = nullptr;

        mAllocationAllocator.Free(allocation);
    }
}