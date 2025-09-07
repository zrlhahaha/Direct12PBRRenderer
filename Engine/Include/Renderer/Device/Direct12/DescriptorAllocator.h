#pragma once
#include "MemoryAllocator.h"

namespace MRenderer
{
    class D3D12CPUDescriptorHeap;
    class D3D12GPUDescriptorHeap;
    class D3D12Resource;

    struct CPUDescriptor
    {
        friend D3D12CPUDescriptorHeap;
    public:
        CPUDescriptor() 
            : mCPUDescriptorHandle{}, mHeapType{}, mSourceDescriptorHeap(nullptr)
        {
        }

        CPUDescriptor(CD3DX12_CPU_DESCRIPTOR_HANDLE cpu_handle, D3D12_DESCRIPTOR_HEAP_TYPE heap_type, ObjectHandle handle, D3D12CPUDescriptorHeap* source)
            : mCPUDescriptorHandle(cpu_handle), mHeapType(heap_type), mObjectHandle(handle), mSourceDescriptorHeap(source)
        {
        }

        CPUDescriptor(const CPUDescriptor& other) = delete;

        CPUDescriptor(CPUDescriptor&& other) 
            : CPUDescriptor()
        {
            swap(*this, other);
        }

        ~CPUDescriptor();

        CPUDescriptor& operator=(CPUDescriptor other) 
        {
            swap(*this, other);
            return *this;
        }

        inline bool Empty() const{ return mSourceDescriptorHeap == nullptr;}
        inline CD3DX12_CPU_DESCRIPTOR_HANDLE CPUDescriptorHandle() const { return mCPUDescriptorHandle;}
        inline D3D12CPUDescriptorHeap* SourceHeap() {return mSourceDescriptorHeap;}
        inline D3D12_DESCRIPTOR_HEAP_TYPE HeapType() const { return mHeapType; }

        friend void swap(CPUDescriptor& lhs, CPUDescriptor& rhs) 
        {
            std::swap(lhs.mCPUDescriptorHandle, rhs.mCPUDescriptorHandle);
            std::swap(lhs.mHeapType, rhs.mHeapType);
            std::swap(lhs.mSourceDescriptorHeap, rhs.mSourceDescriptorHeap);
            std::swap(lhs.mObjectHandle, rhs.mObjectHandle);
        }

    protected:
        // 0 bytes
        CD3DX12_CPU_DESCRIPTOR_HANDLE mCPUDescriptorHandle;
        // 8 bytes
        D3D12_DESCRIPTOR_HEAP_TYPE mHeapType;
        // 12 bytes

        ObjectHandle mObjectHandle;
        // 16 bytes
        D3D12CPUDescriptorHeap* mSourceDescriptorHeap;
        // 24 bytes

    public:

    };

    static_assert(sizeof(CPUDescriptor) == 24);

    struct GPUDescriptor
    {
        friend D3D12GPUDescriptorHeap;
    protected:
        GPUDescriptor() 
            :mCPUDescriptorHandle{}, mGPUDescriptorHandle{}, mObjectHandle{}, mSourceDescriptorHeap(nullptr)
        {
        }

        GPUDescriptor(CD3DX12_CPU_DESCRIPTOR_HANDLE cpu_handle, CD3DX12_GPU_DESCRIPTOR_HANDLE gpu_handle, D3D12_DESCRIPTOR_HEAP_TYPE type, ObjectHandle object_handle, D3D12GPUDescriptorHeap* source)
            :mCPUDescriptorHandle(cpu_handle), mGPUDescriptorHandle(gpu_handle), mHeapType(type), mObjectHandle(object_handle), mSourceDescriptorHeap(source)
        {
        }

    public:
        inline bool Empty() const
        {
            return mSourceDescriptorHeap == nullptr;
        }

        const ID3D12DescriptorHeap* Heap() const;
        GPUDescriptor OffsetDescriptor(uint16 offset) const;
        inline D3D12GPUDescriptorHeap* SourceHeap() const { return mSourceDescriptorHeap;}
        inline CD3DX12_GPU_DESCRIPTOR_HANDLE GPUDescriptorHandle() const { return mGPUDescriptorHandle; }
        inline CD3DX12_CPU_DESCRIPTOR_HANDLE CPUDescriptorHandle() const { return mCPUDescriptorHandle; }
        inline D3D12_DESCRIPTOR_HEAP_TYPE HeapType() const { return mHeapType; }

    protected:
        // for debug purpose, object_handle.page_index should be exactly the index of descriptor in the heap
        uint32 CPUHeapIndex() const;
        uint32 GPUHeapIndex() const;

    protected:
        // 0 bytes
        CD3DX12_CPU_DESCRIPTOR_HANDLE mCPUDescriptorHandle;
        // 8 bytes
        CD3DX12_GPU_DESCRIPTOR_HANDLE mGPUDescriptorHandle;
        // 16 bytes
        D3D12_DESCRIPTOR_HEAP_TYPE mHeapType;

        // 20 bytes
        ObjectHandle mObjectHandle;
        // 24 bytes
        D3D12GPUDescriptorHeap* mSourceDescriptorHeap;
        // 32 bytes

    };

    static_assert(sizeof(GPUDescriptor) == 32);

    class D3D12CPUDescriptorHeap
    {
    public:
        static constexpr uint32 DescriptorPageSize = 1024;

    public:
        D3D12CPUDescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type)
            :mDevice(device), mMeta(PageCapacity()), mHeapType(type)
        {
            mDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(mHeapType);
        }

        ~D3D12CPUDescriptorHeap()
        {
        }

        D3D12CPUDescriptorHeap(const D3D12CPUDescriptorHeap&) = delete;
        D3D12CPUDescriptorHeap& operator=(const D3D12CPUDescriptorHeap&) = delete;

        CPUDescriptor Allocate()
        {
            ObjectHandle handle = mMeta.Allocate();
            ASSERT(handle.page_index <= mHeaps.size());

            if (handle.page_index == mHeaps.size())
            {
                D3D12_DESCRIPTOR_HEAP_DESC desc{
                   .Type = mHeapType,
                   .NumDescriptors = PageCapacity(),
                   .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
                };

                ID3D12DescriptorHeap* heap;
                ThrowIfFailed(mDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap)));

                mHeaps.emplace_back(heap);
            }

            return CPUDescriptor(
                CD3DX12_CPU_DESCRIPTOR_HANDLE(mHeaps[handle.page_index]->GetCPUDescriptorHandleForHeapStart(), handle.offset, DescriptorSize()),
                mHeapType,
                handle,
                this
            );
        }

        void Free(CPUDescriptor& descriptor)
        {
            ASSERT(descriptor.mSourceDescriptorHeap == this);
            mMeta.Free(descriptor.mObjectHandle);
        }

        inline uint32 PageCapacity() const
        {
            return DescriptorPageSize;
        }

        inline uint32 DescriptorSize() const
        {
            return mDescriptorSize;
        }

    private:
        ID3D12Device* mDevice;
        std::vector<ComPtr<ID3D12DescriptorHeap>> mHeaps;
        RandomObjectAllocatorMeta mMeta;
        D3D12_DESCRIPTOR_HEAP_TYPE mHeapType;
        uint32 mDescriptorSize;
    };

    class CPUDescriptorAllocator
    {
    public:
        CPUDescriptorAllocator(ID3D12Device* device)
            :mHeaps{
                D3D12CPUDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV),
                D3D12CPUDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER),
                D3D12CPUDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV),
                D3D12CPUDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV)
            }
        {
        };

        ~CPUDescriptorAllocator() {};

        CPUDescriptorAllocator(const CPUDescriptorAllocator&) = delete;
        CPUDescriptorAllocator& operator=(const CPUDescriptorAllocator&) = delete;

        inline CPUDescriptor Allocate(D3D12_DESCRIPTOR_HEAP_TYPE heap_type)
        {
            return mHeaps[heap_type].Allocate();
        }

        inline void Free(CPUDescriptor& descriptor)
        {
            return mHeaps[descriptor.HeapType()].Free(descriptor);
        }
    protected:
        D3D12CPUDescriptorHeap mHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];
    };


    class D3D12GPUDescriptorHeap
    {
    public:
        static constexpr size_t DescriptorPageSize = 1024;

    public:
        D3D12GPUDescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type)
            :mDevice(device), mMeta(PageCapacity()), mHeapType(type)
        {
        }

        GPUDescriptor Allocate(uint32 size = 1)
        {
            if (!size) 
            {
                return GPUDescriptor();
            }

            ObjectHandle handle = mMeta.AllocateRange(size);

            if (handle.page_index == mHeaps.size())
            {
                D3D12_DESCRIPTOR_HEAP_DESC desc{
                    .Type = mHeapType,
                    .NumDescriptors = static_cast<uint32>(PageCapacity()),
                    .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
                };

                ID3D12DescriptorHeap* heap = nullptr;
                ThrowIfFailed(mDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap)));

                mHeaps.emplace_back(heap);
            }

            GPUDescriptor descriptor(
                CD3DX12_CPU_DESCRIPTOR_HANDLE(
                    mHeaps[handle.page_index]->GetCPUDescriptorHandleForHeapStart(),
                    handle.offset,
                    DescriptorSize()
                ),
                CD3DX12_GPU_DESCRIPTOR_HANDLE(
                    mHeaps[handle.page_index]->GetGPUDescriptorHandleForHeapStart(),
                    handle.offset,
                    DescriptorSize()
                ),
                mHeapType,
                handle,
                this
            );
            return descriptor;
        }

        std::vector<ID3D12DescriptorHeap*> GetAllPages() const
        {
            std::vector<ID3D12DescriptorHeap*> heaps(mHeaps.size());
            for (size_t i = 0; i < mHeaps.size(); i++)
            {
                heaps[i] = mHeaps[i].Get();
            }
            return heaps;
        }

        inline const ID3D12DescriptorHeap* GetPage(size_t page_index) const
        {
            return mHeaps[page_index].Get();
        }

        inline void Reset()
        {
            mMeta.Reset();
        }

        inline uint32 PageCapacity() const
        {
            return DescriptorPageSize;
        }

        inline uint32 DescriptorSize() const
        {
            return mDevice->GetDescriptorHandleIncrementSize(mHeapType);
        }

    protected:
        ID3D12Device* mDevice;
        std::vector<ComPtr<ID3D12DescriptorHeap>> mHeaps;
        FrameObjectAllocatorMeta mMeta;
        D3D12_DESCRIPTOR_HEAP_TYPE mHeapType;
    };


    class GPUDescriptorAllocator
    {
    public:
        GPUDescriptorAllocator(ID3D12Device* device)
            :mHeaps{
                D3D12GPUDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV),
                D3D12GPUDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER),
            }
        {
        }

        GPUDescriptorAllocator(const GPUDescriptorAllocator&) = delete;
        GPUDescriptorAllocator& operator=(const GPUDescriptorAllocator&) = delete;

        inline GPUDescriptor Allocate(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32 count = 1)
        {
            ASSERT(type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
            return mHeaps[type].Allocate(count);
        }

        inline void Reset()
        {
            for (size_t i = 0; i < std::size(mHeaps); i++)
            {
                mHeaps[i].Reset();
            }
        }

    protected:
        /*
        only contain D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV and D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER heaps.
        *
        You can only bind descriptor heaps of type D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV and D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER.
        Only one descriptor heap of each type can be set at one time, which means a maximum of 2 heaps(one sampler, one CBV / SRV / UAV) can be set at one time.
        #ref: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setdescriptorheaps
        */
        D3D12GPUDescriptorHeap mHeaps[2];
    };
}