#include "Renderer/Device/Direct12/DeviceResource.h"
#include "Renderer/Device/Direct12/D3D12Device.h"
#include "DirectXTex.h"

namespace MRenderer 
{
    D3D12Resource::D3D12Resource(D3D12Resource&& other)
        :D3D12Resource()
    {
        Swap(*this, other);
    }

    D3D12Resource::~D3D12Resource()
    {
        if (mAllocation)
        {
            GD3D12Device->ReleaseResource(mAllocation);
        }
        mAllocation = nullptr;
        mResource = nullptr;
    }

    D3D12Resource& D3D12Resource::operator=(D3D12Resource&& other)
    {
        Swap(*this, other);
        return *this;
    }

    void D3D12Resource::TransitionBarrier(ID3D12GraphicsCommandList* command_list, D3D12_RESOURCE_STATES state)
    {
        ASSERT(mResource);
        if (state != mResourceState)
        {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(mResource, mResourceState, state);
            command_list->ResourceBarrier(1, &barrier);

            mResourceState = state;
        }
    }

    DeviceTexture2DArray::DeviceTexture2DArray(D3D12Resource resource)
        :DeviceTexture(std::move(resource)), mMipSliceArrayUAV(MipLevels())
    {
    }

    void DeviceConstantBuffer::SetConstantBufferView(std::array<ConstantBufferView, FrameResourceCount> view_array) 
    { 
        mConstantBufferViewArray = std::move(view_array); 
    }

    D3D12Resource* DeviceConstantBuffer::GetCurrentResource()
    {
        return &mBufferArray[GD3D12Device->FrameIndex()];
    }

    ConstantBufferView* DeviceConstantBuffer::GetCurrendConstantBufferView()
    { 
        ASSERT(!mConstantBufferViewArray[GD3D12Device->FrameIndex()].Empty());
        return &mConstantBufferViewArray[GD3D12Device->FrameIndex()];
    }

    void DeviceConstantBuffer::CommitData(const void* data, uint32 size)
    {
        ASSERT(size < mBufferSize);
        memcpy(mConstantBufferViewArray[GD3D12Device->FrameIndex()].Resource()->GetMappedPtr(), data, size);
    }

    void DeviceStructuredBuffer::Commit(const void* data, uint32 size)
    {
        GD3D12Device->CommitBuffer(&mBuffer, data, size);
    }

}