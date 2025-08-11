#include "Renderer/Device/Direct12/D3D12Device.h"
#include "Renderer/Device/Direct12/D3D12CommandList.h"

namespace MRenderer
{
    D3D12CommandList::D3D12CommandList(ID3D12Device* device)
        :mDevice(device), mFenceValue(0), mOpened(false)
    {
        for (uint32 i = 0; i < FrameResourceCount; i++) 
        {
            ID3D12GraphicsCommandList* command_list;
            ID3D12CommandAllocator* command_allocator;

            ThrowIfFailed(mDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocator)));
            ThrowIfFailed(mDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator, nullptr, IID_PPV_ARGS(&command_list)));
            ThrowIfFailed(command_list->Close());

            mCommandList[i] = command_list;
            mCommandAllocator[i] = command_allocator;
            mGPUDescriptorAllocator[i] = std::make_unique<GPUDescriptorAllocator>(mDevice);
        }
    }

    void D3D12CommandList::SetGeometry(const DeviceVertexBuffer* vb, const DeviceIndexBuffer* ib)
    {
        if (vb != mVertexBuffer)
        {
            mVertexBuffer = vb;
            GetCommandList()->IASetVertexBuffers(0, 1, &vb->VertexBufferView());
        }

        if (ib != mIndexBuffer)
        {
            mIndexBuffer = ib;
            GetCommandList()->IASetIndexBuffer(&ib->IndexBufferView());
        }
    }

    void D3D12CommandList::BeginFrame()
    {
        ASSERT(!mOpened);

        // reset internal state
        mOpened = true;
        mVertexBuffer = nullptr;
        mIndexBuffer = nullptr;
        mPso = {};
        mResourceBinding = nullptr;
        mGraphicsConstantBufferViewArray = {};
        mComputeConstantBufferViewArray = {};

        mFrameIndex = (mFrameIndex + 1) % FrameResourceCount;

        // reset command list, command allocator
        ThrowIfFailed(mCommandAllocator[mFrameIndex]->Reset());
        ThrowIfFailed(mCommandList[mFrameIndex]->Reset(mCommandAllocator[mFrameIndex].Get(), nullptr));
        mGPUDescriptorAllocator[mFrameIndex]->Reset();

        // some global setting
        GetCommandList()->RSSetViewports(1, &GD3D12Device->mViewport);
        GetCommandList()->RSSetScissorRects(1, &GD3D12Device->mScissorRect);
        GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        GetCommandList()->SetGraphicsRootSignature(GD3D12Device->GetRootSignature());
        GetCommandList()->SetComputeRootSignature(GD3D12Device->GetRootSignature());

        // clean back buffer and depth stencil
        DeviceBackBuffer* rt = GD3D12Device->GetCurrentBackBuffer();
        ClearRenderTarget(rt->GetRenderTargetView());
    }

    void D3D12CommandList::EndFrame()
    {
        ASSERT(mOpened);

        mOpened = false;
        ThrowIfFailed(GetCommandList()->Close());
    }

    void D3D12CommandList::DrawScreen(ShadingState* shading_state)
    {
        DeviceVertexBuffer* vertices =  GD3D12Device->GetScreenMeshVertices();
        DeviceIndexBuffer* indicies = GD3D12Device->GetScreenMeshIndicies();

        // mesh
        SetGeometry(vertices, indicies);

        // shader constant buffer
        ASSERT(shading_state->GetConstantBuffer());
        SetGrphicsConstant(EConstantBufferType_Shader, shading_state->GetConstantBuffer()->GetCurrendConstantBufferView());

        // bind shader resource
        SetResourceBinding(shading_state->GetResourceBinding(), false);

        // issue draw call
        GetCommandList()->DrawIndexedInstanced(vertices->VertexCount(), 1, 0, 0, 0);
    }

    void D3D12CommandList::DrawMesh(ShadingState* shading_state, EVertexFormat vertex_format, DeviceVertexBuffer* vertices, DeviceIndexBuffer* indicies, uint32 index_begin, uint32 index_count)
    {
        // mesh
        SetGeometry(vertices, indicies);

        if (shading_state->GetConstantBuffer())
        {
            SetGrphicsConstant(EConstantBufferType_Shader, shading_state->GetConstantBuffer()->GetCurrendConstantBufferView());
        }

        // bind shader resource
        SetResourceBinding(shading_state->GetResourceBinding(), false);

        // issue draw call
        GetCommandList()->DrawIndexedInstanced(index_count, 1, index_begin, 0, 0);
    }

    void D3D12CommandList::Dispatch(ShadingState* shading_state, uint32 thread_group_count_x, uint32 thread_group_count_y, uint32 thread_group_count_z)
    {
        // bind shader resource
        SetResourceBinding(shading_state->GetResourceBinding(), true);

        // set shader constant
        if (shading_state->GetConstantBuffer())
        {
            SetComputeConstant(EConstantBufferType_Shader, shading_state->GetConstantBuffer()->GetCurrendConstantBufferView());
        }

        // compute pso
        ASSERT(shading_state->GetShader());
        SetComputePipelineState(shading_state->GetShader());

        // issue compute call
        GetCommandList()->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z);
    }

    void D3D12CommandList::SetGrphicsConstant(EConstantBufferType type, ConstantBufferView* view)
    {
        if (mGraphicsConstantBufferViewArray[type] != view)
        {
            mGraphicsConstantBufferViewArray[type] = view;
            GetCommandList()->SetGraphicsRootConstantBufferView(type, view->Resource()->Resource()->GetGPUVirtualAddress());
        }
    }

    void D3D12CommandList::SetComputeConstant(EConstantBufferType type, ConstantBufferView* view)
    {
        if (mComputeConstantBufferViewArray[type] != view)
        {
            mComputeConstantBufferViewArray[type] = view;
            GetCommandList()->SetComputeRootConstantBufferView(type, view->Resource()->Resource()->GetGPUVirtualAddress());
        }
    }

    void D3D12CommandList::CopyTexture(D3D12Resource* src, D3D12Resource* dest)
    {
        src->TransitionBarrier(GetCommandList(), D3D12_RESOURCE_STATE_COPY_SOURCE);
        dest->TransitionBarrier(GetCommandList(), D3D12_RESOURCE_STATE_COPY_DEST);
        GetCommandList()->CopyResource(dest->Resource(), src->Resource());
    }

    void D3D12CommandList::Present(DeviceTexture2D* tex)
    {
        DeviceBackBuffer* back_buffer = GD3D12Device->GetCurrentBackBuffer();
        CopyTexture(tex->Resource(), back_buffer->Resource());
        back_buffer->Resource()->TransitionBarrier(GetCommandList(), D3D12_RESOURCE_STATE_PRESENT);
    }

    void D3D12CommandList::ClearRenderTarget(RenderTargetView* view)
    {
        ASSERT(!view->Empty());

        // ref frome MSDN: For ClearRenderTargetView, the state must be D3D12_RESOURCE_STATE_RENDER_TARGET.
        // rt barrier
        view->Resource()->TransitionBarrier(GetCommandList(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        // clear rt
        constexpr float clean_value[] = { 0.0f, 0.0f, 0.0f, 0.0f };
        GetCommandList()->ClearRenderTargetView(view->Descriptor()->CPUDescriptorHandle(), clean_value, 0, nullptr);
    }

    void D3D12CommandList::ClearDepthStencil(DepthStencilView* view)
    {
        ASSERT(!view->Empty());

        // ref frome MSDN: For ClearDepthStencilView, the state must be in the state D3D12_RESOURCE_STATE_DEPTH_WRITE.
        view->Resource()->TransitionBarrier(GetCommandList(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
        
        GetCommandList()->ClearDepthStencilView(view->Descriptor()->CPUDescriptorHandle(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0, 0, 0, nullptr);
    }

    void D3D12CommandList::SetRenderTarget(std::array<RenderTargetView*, MaxRenderTargets> rtv_array, uint32 num_rt, DepthStencilView* dsv/*=nullptr*/)
    {
        // bind render target
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, MaxRenderTargets> rt_descriptors;
        for (uint32 i = 0; i < num_rt; i++)
        {
            ASSERT(!rtv_array[i]->Empty());

            rt_descriptors[i] = rtv_array[i]->Descriptor()->CPUDescriptorHandle();
            rtv_array[i]->Resource()->TransitionBarrier(GetCommandList(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        }

        // bind depth stencil
        if (dsv)
        {
            ASSERT(!dsv->Empty());

            dsv->Resource()->TransitionBarrier(GetCommandList(), D3D12_RESOURCE_STATE_DEPTH_WRITE);

            CD3DX12_CPU_DESCRIPTOR_HANDLE cpu_handle = dsv->Descriptor()->CPUDescriptorHandle();
            GetCommandList()->OMSetRenderTargets(num_rt, rt_descriptors.data(), false, &cpu_handle);
        }
        else 
        {
            GetCommandList()->OMSetRenderTargets(num_rt, rt_descriptors.data(), false, nullptr);
        }
    }

    bool D3D12CommandList::IsOpen()
    {
        return mOpened;
    }

    ID3D12GraphicsCommandList* D3D12CommandList::GetCommandList()
    {
        return mCommandList[mFrameIndex].Get();
    }

    ID3D12CommandAllocator* D3D12CommandList::GetCommandAllocator()
    {
        return mCommandAllocator[mFrameIndex].Get();
    }

    void D3D12CommandList::SetStencilRef(uint8 ref)
    {
        GetCommandList()->OMSetStencilRef(ref);
    }

    D3D12RootParameters D3D12CommandList::AllocateRootParameter(uint32 srv_size, uint32 uav_size, uint32 sampler_size)
    {
        GPUDescriptor srv_start = mGPUDescriptorAllocator[mFrameIndex]->Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, srv_size + uav_size);
        GPUDescriptor uav_start = srv_start.OffsetDescriptor(srv_size);
        GPUDescriptor sampler_start = mGPUDescriptorAllocator[mFrameIndex]->Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, sampler_size);

        return D3D12RootParameters(srv_start, srv_size, uav_start, uav_size, sampler_start, sampler_size);
    }

    void D3D12CommandList::SetResourceBinding(const ResourceBinding* resource_binding, bool is_compute)
    {
        if (mResourceBinding == resource_binding && mIsCompute == is_compute) 
        {
            return;
        }

        mResourceBinding = resource_binding;
        mIsCompute = is_compute;

        D3D12RootParameters root_parameter = AllocateRootParameter(
            ShaderResourceMaxTexture,
            ShaderResourceMaxUAV,
            ShaderResourceMaxSampler
        );

        // srv
        for (uint32 i = 0; i < ShaderResourceMaxTexture; i++)
        {
            ShaderResourceView* view = resource_binding->SRVs[i];
            if (view)
            {
                root_parameter.StageSRV(i, view->Descriptor());

                D3D12Resource* resource = view->Resource();
                if (view->Resource()->Format() == static_cast<ETextureFormat>(D3D12Device::DepthStencilFormat))
                {
                    resource->TransitionBarrier(GetCommandList(), D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                }
                else 
                {
                    resource->TransitionBarrier(GetCommandList(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
                }
            }
        }

        // uav
        for (uint32 i = 0; i < ShaderResourceMaxUAV; i++)
        {
            UnorderAccessView* view = resource_binding->UAVs[i];
            if (view)
            {
                root_parameter.StageUAV(i, view->Descriptor());
                view->Resource()->TransitionBarrier(GetCommandList(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
        }

        // sampler
        static std::shared_ptr<DeviceSampler> sampler[] = {
            GD3D12Device->CreateSampler(ESamplerFilter_Point, ESamplerAddressMode_Wrap),
            GD3D12Device->CreateSampler(ESamplerFilter_Point, ESamplerAddressMode_Clamp),
            GD3D12Device->CreateSampler(ESamplerFilter_Linear, ESamplerAddressMode_Wrap),
            GD3D12Device->CreateSampler(ESamplerFilter_Linear, ESamplerAddressMode_Clamp),
            GD3D12Device->CreateSampler(ESamplerFilter_Anisotropic, ESamplerAddressMode_Wrap),
            GD3D12Device->CreateSampler(ESamplerFilter_Anisotropic, ESamplerAddressMode_Clamp),
        };

        for (uint32 i = 0; i < std::size(sampler); i++) 
        {
            root_parameter.StageSampler(i, sampler[i]->Descriptor());
        }

        if (is_compute) 
        {
            root_parameter.BindCompute(GetCommandList());
        }
        else
        {
            root_parameter.BindGraphics(GetCommandList());
        }
    }

    void D3D12CommandList::SetGraphicsPipelineState(EVertexFormat format, const PipelineStateDesc* pipeline_desc, const RenderPassStateDesc* pass_desc, const D3D12ShaderProgram* program)
    {
        ASSERT(pipeline_desc && pass_desc && program);
        
        PipelineStateKey key =
        {
            .Desc =
            {
                .PipelineDesc = *pipeline_desc,
                .RenderPassState = *pass_desc,
                .VertexFormat = format,
                .ShaderHashCode = program->mHashCode,
                .IsCompute = false
            }
        };

        ASSERT(key != PipelineStateKey{});
        if (mPso == key) // skip if it's same as previous pipeline state
        {
            return;
        }

        PipelineStateObject* pso = nullptr;
        auto it = mPSOTable.find(key);
        if (it != mPSOTable.end())
        {
            pso = it->second.get();
        }
        else
        {
            mPSOTable[key] = GD3D12Device->CreateGraphicsPipelineStateObject(format, pipeline_desc, pass_desc, program);
            pso = mPSOTable[key].get();
        }

        GetCommandList()->SetPipelineState(pso->mPSO.Get());
    }

    void D3D12CommandList::SetComputePipelineState(const D3D12ShaderProgram* program)
    {
        ASSERT(program);

        PipelineStateObject* pso = nullptr;
        PipelineStateKey key =
        {
            .Desc =
            {
                .PipelineDesc = {},
                .RenderPassState = {},
                .VertexFormat = {},
                .ShaderHashCode = program->mHashCode,
                .IsCompute = true
            }
        };

        ASSERT(key != PipelineStateKey{});
        if (key == mPso) 
        {
            return;
        }

        auto it = mPSOTable.find(key);
        if (it != mPSOTable.end())
        {
            pso = it->second.get();
        }
        else
        {
            mPSOTable[key] = GD3D12Device->CreateComputePipelineStateObject(program);
            pso = mPSOTable[key].get();
        }

        mPso = key;
        GetCommandList()->SetPipelineState(pso->mPSO.Get());
    }
}
