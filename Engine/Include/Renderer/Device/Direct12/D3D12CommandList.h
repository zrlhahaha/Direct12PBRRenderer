#pragma once
#include <map>

#include "D3D12Device.h"
#include "DeviceResource.h"
#include "Renderer/Pipeline/IPipeline.h"


namespace MRenderer 
{
    union PipelineStateKey
    {
        struct Desc {
            PipelineStateDesc PipelineDesc;
            GraphicsPassPsoDesc RenderPassState;
            EVertexFormat VertexFormat;
            uint8 ShaderHashCode;
            bool IsCompute;
            uint8 Padding[3];
        } Desc;

        struct Key
        {
            uint64 a;
            uint64 b;
            uint64 c;
        } Key;

        static_assert(sizeof(Desc) == sizeof(Key));

        bool operator==(const PipelineStateKey& other) const
        {
            return
                Key.a == other.Key.a &&
                Key.b == other.Key.b &&
                Key.c == other.Key.c;
        }

        bool operator!=(const PipelineStateKey& other) const 
        {
            return !(*this == other);
        }

        bool operator<(const PipelineStateKey& other) const
        {
            if (Key.a != other.Key.a) 
            {
                return Key.a < other.Key.a;
            }

            if (Key.b != other.Key.b)
            {
                return Key.b < other.Key.b;
            }

            return Key.c < other.Key.c;
        }
    };

    inline const PipelineStateKey EmptyPSO = PipelineStateKey{};

    static_assert(sizeof(PipelineStateKey) == 3 * sizeof(uint64));
}

namespace MRenderer
{
    class ShadingState;

    class D3D12CommandList 
    {
    public:
        D3D12CommandList(ID3D12Device* device);

        // API for frame loop
        void BeginFrame();
        void EndFrame();
        void Present(DeviceTexture2D* tex);
        bool IsOpen();
        ID3D12GraphicsCommandList* GetCommandList();
        ID3D12CommandAllocator* GetCommandAllocator();

        // draw screen
        void DrawScreen(ShadingState* shading_state);

        // draw a single mesh
        void DrawMesh(ShadingState* shading_state, EVertexFormat vertex_format, DeviceVertexBuffer* vertices, DeviceIndexBuffer* indicies, uint32 index_begin, uint32 index_count);

        // copy gpu resource from @src to @dest
        void CopyTexture(D3D12Resource* src, D3D12Resource* dest);

        // copy constant buffer data to gpu
        void SetGrphicsConstant(EConstantBufferType type, ConstantBufferView* cbuffer);
        void SetComputeConstant(EConstantBufferType type, ConstantBufferView* cbuffer);

        // clear render target
        void ClearRenderTarget(RenderTargetView* view);
        void ClearDepthStencil(DepthStencilView* tex);

        // set render target
        void SetRenderTarget(std::array<RenderTargetView*, MaxRenderTargets> rts, uint32 num_rt, DepthStencilView* view=nullptr);

        // issue compute command
        void Dispatch(ShadingState* shading_state, uint32 thread_group_count_x, uint32 thread_group_count_y, uint32 thread_group_count_z);
        
        // bind shader resource to the gpu pipeline
        void SetResourceBinding(const ResourceBinding* resource_binding, bool is_compute);

        // set pipeline state
        void SetGraphicsPipelineState(EVertexFormat format, const PipelineStateDesc* pipeline_desc, const GraphicsPassPsoDesc* pass_desc, const D3D12ShaderProgram* program);
        void SetComputePipelineState(const D3D12ShaderProgram* program);

        // set stencil reference value
        void SetStencilRef(uint8 ref);

    protected:
        D3D12RootParameters AllocateRootParameter(uint32 srv_size, uint32 uav_size, uint32 sampler_size);
        void SetGeometry(const DeviceVertexBuffer* vb, const DeviceIndexBuffer* ib);

    protected:
        ID3D12Device* mDevice;
        ComPtr<ID3D12CommandAllocator> mCommandAllocator[FrameResourceCount];
        ComPtr<ID3D12GraphicsCommandList> mCommandList[FrameResourceCount];
        std::unique_ptr<GPUDescriptorAllocator> mGPUDescriptorAllocator[FrameResourceCount];
        ComPtr<ID3D12Fence> mFence;
        HANDLE mFenceEvent;
        uint32 mFenceValue;
        uint32 mFrameIndex;
        bool mOpened;

        // state book-keeping
        const DeviceVertexBuffer* mVertexBuffer;
        const DeviceIndexBuffer* mIndexBuffer;
        const ResourceBinding* mResourceBinding;
        PipelineStateKey mPso;
        bool mIsCompute;
        std::array<const ConstantBufferView*, EConstantBufferType_Total> mGraphicsConstantBufferViewArray;
        std::array<const ConstantBufferView*, EConstantBufferType_Total> mComputeConstantBufferViewArray;

        // local pso cache
        std::map<PipelineStateKey, std::shared_ptr<PipelineStateObject>> mPSOTable;
    };
}