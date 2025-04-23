#include "Renderer/Device/Direct12/DescriptorAllocator.h"

namespace MRenderer
{
    // for debug purpose, object_handle.page_index should be exactly the index of descriptor in the heap
    uint32 GPUDescriptor::CPUHeapIndex() const
    {
        auto heap = const_cast<ID3D12DescriptorHeap*>(mSourceDescriptorHeap->GetPage(mObjectHandle.page_index));
        D3D12_CPU_DESCRIPTOR_HANDLE heap_start = heap->GetCPUDescriptorHandleForHeapStart();
        uint32 offset = static_cast<uint32>(mCPUDescriptorHandle.ptr - heap_start.ptr);

        ASSERT(offset % mSourceDescriptorHeap->DescriptorSize() == 0);
        uint32 index = offset / mSourceDescriptorHeap->DescriptorSize();

        ASSERT(index == mObjectHandle.offset);
        return index;
    }

    uint32 GPUDescriptor::GPUHeapIndex() const
    {
        auto heap = const_cast<ID3D12DescriptorHeap*>(mSourceDescriptorHeap->GetPage(mObjectHandle.page_index));
        D3D12_GPU_DESCRIPTOR_HANDLE heap_start = heap->GetGPUDescriptorHandleForHeapStart();
        uint32 offset = static_cast<uint32>(mGPUDescriptorHandle.ptr - heap_start.ptr);

        ASSERT(offset % mSourceDescriptorHeap->DescriptorSize() == 0);
        uint32 index = offset / mSourceDescriptorHeap->DescriptorSize();

        ASSERT(index == mObjectHandle.offset);
        return index;
    }
    const ID3D12DescriptorHeap* GPUDescriptor::Heap() const
    {
        return mSourceDescriptorHeap->GetPage(mObjectHandle.page_index);
    }

    GPUDescriptor GPUDescriptor::OffsetDescriptor(uint16 offset) const
    {
        ASSERT(mObjectHandle.offset + offset < mSourceDescriptorHeap->DescriptorPageSize);

        GPUDescriptor ret(*this);
        ret.mCPUDescriptorHandle.Offset(offset, mSourceDescriptorHeap->DescriptorSize());
        ret.mGPUDescriptorHandle.Offset(offset, mSourceDescriptorHeap->DescriptorSize());
        ret.mObjectHandle.offset = static_cast<uint16>(mObjectHandle.offset + offset);

        return ret;
    }

    CPUDescriptor::~CPUDescriptor()
    {
        if (mSourceDescriptorHeap)
        {
            mSourceDescriptorHeap->Free(*this);
            mSourceDescriptorHeap = nullptr;
        }
    }
}