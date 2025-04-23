#pragma once
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3d12shader.h>
#include <filesystem>

#include "D3DUtils.h"
#include "Resource/ResourceDef.h"
#include "Resource/Shader.h"


namespace MRenderer
{
    class D3D12Device;
    class D3D12CommandList;


    extern ID3D12Device* GD3D12RawDevice;
    extern D3D12Device* GD3D12Device;


    class PipelineStateObject 
    {
    public: 
        PipelineStateObject(ID3D12PipelineState* pso) 
            :mPSO(pso)
        {
        }

    public:
        ComPtr< ID3D12PipelineState> mPSO;
    };

    class D3D12RootParameters
    {
    public:
        // srv_uav, sampler, up to 2 descriptor heaps are needed to bind to gpu
        static constexpr size_t GPUDescriptorHeapCount = 2;

    protected:
        class D3D12DescriptorTable
        {
            friend class D3D12RootParameters;
        public:
            D3D12DescriptorTable(const GPUDescriptor& start, uint32 size)
                :mStart(start), mSize(size), mMask(0)
            {
                ASSERT(size < (sizeof(mMask) * CHAR_BIT)); // support up to 16 descriptor for now
            }

            inline void StageDescriptor(ID3D12Device* device, uint32 index, const CPUDescriptor* cpu_descriptor, size_t size)
            {
                ASSERT(index + size <= mSize);
                ASSERT(!mStart.Empty());

                for (size_t i = 0; i < size; i++)
                {
                    ASSERT(!(mMask & (1 << (index + i)))); // break when reassigning descriptor on the same slot
                    mMask = mMask | (1 << (index + i));
                }

                device->CopyDescriptorsSimple(size, mStart.OffsetDescriptor(index).CPUDescriptorHandle(), cpu_descriptor->CPUDescriptorHandle(), mStart.HeapType());
            }

            // fill the empty slot with the given @descriptor
            void FillDescriptor(ID3D12Device* device, CPUDescriptor* descriptor)
            {
                ASSERT(!mStart.Empty());
                ASSERT(mStart.HeapType() == descriptor->HeapType());
                // fill descriptor heap
                for (uint32 i = 0; i < mSize; i++)
                {
                    if (!(mMask & (1 << i)))
                    {
                        device->CopyDescriptorsSimple(1, mStart.OffsetDescriptor(i).CPUDescriptorHandle(), descriptor->CPUDescriptorHandle(), mStart.HeapType());
                    }
                }
            }

            // check if all descriptors are filled
            void AssertDescriptorFull()
            {
                ASSERT(mMask == (1 << mSize) - 1);
            }

            inline const ID3D12DescriptorHeap* GetDescriptorHeap() const { ASSERT(!mStart.Empty()); return mStart.Heap(); }
            inline bool Empty() const { return mStart.Empty();}
            inline uint32 Size() const { return mSize; }

        protected:
            GPUDescriptor mStart;
            uint16 mSize;
            uint16 mMask;
        };

    public:
        // gpu descriptor will be released at the end of the frame, no desctructor is needed
        D3D12RootParameters(GPUDescriptor srv_start, uint32 srv_size, GPUDescriptor uav_start, uint32 uav_size, GPUDescriptor sampler_start, uint32 sampler_size)
            :mSRVs(srv_start, srv_size), mUAVs(uav_start, uav_size), mSamplers(sampler_start, sampler_size)
        {
            // srv and uav must be in the same descriptor heap 
            // or they won't be able to bind to gpu at the same time
            if (!mSRVs.Empty() && !mUAVs.Empty()) 
            {
                ASSERT(mSRVs.GetDescriptorHeap() == mUAVs.GetDescriptorHeap());
            }

            mNumHeaps = 0;
            if (!mSRVs.Empty()) 
            {
                mHeaps[mNumHeaps++] = mSRVs.GetDescriptorHeap();
            }

            if (!mSamplers.Empty())  
            {
                mHeaps[mNumHeaps++] = mSamplers.GetDescriptorHeap();
            }
        }

        void StageSRV(uint32 index, const CPUDescriptor* cpu_descriptor, size_t size = 1)
        {
            ASSERT(cpu_descriptor->HeapType() == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            mSRVs.StageDescriptor(GD3D12RawDevice, index, cpu_descriptor, size);
        }

        void StageUAV(uint32 index, const CPUDescriptor* cpu_descriptor, size_t size = 1) 
        {
            ASSERT(cpu_descriptor->HeapType() == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            mUAVs.StageDescriptor(GD3D12RawDevice, index, cpu_descriptor, size);
        }

        void StageSampler(uint32 index, const CPUDescriptor* cpu_descriptor, size_t size = 1)
        {
            ASSERT(cpu_descriptor->HeapType() == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
            mSamplers.StageDescriptor(GD3D12RawDevice, index, cpu_descriptor, size);
        }

        // bind root descriptor tables and descriptor heaps
        void BindGraphics(ID3D12GraphicsCommandList* command_list);
        void BindCompute(ID3D12GraphicsCommandList* command_list);

    protected:
        D3D12DescriptorTable mSRVs;
        D3D12DescriptorTable mUAVs;
        D3D12DescriptorTable mSamplers;
        std::array<const ID3D12DescriptorHeap*, GPUDescriptorHeapCount> mHeaps; // gpu descriptor heaps [SRV heap, Sampler heap]
        uint32 mNumHeaps;
    };

    class D3D12Device
    {
    public:
        D3D12Device(uint32 width, uint32 height);
        ~D3D12Device();

        // non-copyable
        D3D12Device(const D3D12Device&) = delete;
        D3D12Device& operator=(const D3D12Device&) = delete;

        void LogAdapters();

         
    public:
        inline uint32 Width() const { return mWidth;}
        inline uint32 Height() const { return mHeight;}
        inline uint32 FrameIndex() const { return mFrameIndex;}

        inline ShaderResourceView& GetNullSRV() { return mNullSRV; }
        inline UnorderAccessView& GetNullUAV() { return mNullUAV; }
        inline RenderTargetView& GetNullRTV() { return mNullRTV; }

        std::shared_ptr<DeviceVertexBuffer> CreateVertexBuffer(const void* data, uint32 count, uint32 stride);
        std::shared_ptr<DeviceIndexBuffer> CreateIndexBuffer(const uint32* data, uint32 data_size);
        std::shared_ptr<DeviceStructuredBuffer> CreateStructuredBuffer(uint32 data_size, uint32 stride, const void* initial_data=nullptr);
        std::shared_ptr<DeviceTexture2D> CreateTexture2D(uint32 width, uint32 height, ETextureFormat format, const void* initial_data = nullptr, bool unorder_access=false);
        std::shared_ptr<DeviceTexture2DArray> CreateTextureCube(uint32 width, uint32 height, uint32 mip_level, ETextureFormat format, std::array<TextureData, 6>* initial_data=nullptr, bool unorder_access=false);
        std::shared_ptr<DeviceConstantBuffer> CreateConstBuffer(uint32 size);
        std::shared_ptr<DeviceSampler> CreateSampler(ESamplerFilter filter_mode, ESamplerAddressMode address_mode);
        std::shared_ptr<DeviceRenderTarget> CreateRenderTarget(uint32 width, uint32 height, ETextureFormat format = ETextureFormat_R8G8B8A8_UNORM, D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_RENDER_TARGET);
        std::shared_ptr<DeviceDepthStencil> CreateDepthStencil(uint32 width, uint32 height, D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_DEPTH_WRITE);
        std::shared_ptr<PipelineStateObject> CreateGraphicsPipelineStateObject(EVertexFormat format, const PipelineStateDesc* pipeline_desc, const RenderPassStateDesc* pass_desc, const D3D12ShaderProgram* program);
        std::shared_ptr<PipelineStateObject> CreateComputePipelineStateObject(const D3D12ShaderProgram* program);
        
        void UpdateTextureArraySlice(DeviceTexture2DArray* array, uint32 index, const void* data);
        
        // commit data from cpu memory to the default heap, use Map to commit data instead if the @resource is allocated on the upload heap
        void UploadToDefaultHeap(ID3D12Resource* resource, const void* data, uint32 size);

        ID3D12RootSignature* GetRootSignature();
        DeviceBackBuffer* GetCurrentBackBuffer();
        DeviceVertexBuffer* GetScreenMeshVertices();
        DeviceIndexBuffer* GetScreenMeshIndicies();

        void ReleaseResource(MemoryAllocation* res);
        
        void BeginFrame();
        void EndFrame(D3D12CommandList* render_command_list=nullptr);

    private:
        MemoryAllocation* CreateDeviceBuffer(uint32 size, bool unordered_access, const void* initial_data/*=nullptr*/, D3D12_RESOURCE_STATES initial_state/*=D3D12_RESOURCE_STATE_COMMON*/);
        ID3D12RootSignature* CreateRootSignature() const;
        ShaderResourceView CreateShaderResourceView(const D3D12_SHADER_RESOURCE_VIEW_DESC* desc, D3D12Resource* resource);
        UnorderAccessView CreateUnorderedAccessView(const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc, D3D12Resource* resource);
        RenderTargetView CreateRenderTargetView(const D3D12_RENDER_TARGET_VIEW_DESC* desc, D3D12Resource* resource);
        DepthStencilView CreateDepthStencilView(const D3D12_DEPTH_STENCIL_VIEW_DESC* desc, D3D12Resource* resource);
        ConstantBufferView CreateConstantBufferView(const D3D12_CONSTANT_BUFFER_VIEW_DESC* desc, D3D12Resource* resource);

        void ClearResourceCache(uint32 frame_index);
        void LogAdapterOutputs(IDXGIAdapter* adapter);
        void LogOutputDisplayModes(IDXGIOutput* output, DXGI_FORMAT format);
        void CheckFeatureSupport();
        void InitializeInternalResource();
        void WaitForGPUExecution();

    public:
        static constexpr DXGI_FORMAT BackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        static constexpr DXGI_FORMAT DepthStencilFormat = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        static constexpr DXGI_FORMAT DepthStencilSRVFormat = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;

    public:
        CD3DX12_VIEWPORT mViewport;
        CD3DX12_RECT mScissorRect;

    private:
        D3D12ShaderCompiler mCompiler;

        std::unique_ptr<MemoryAllocator> mMemoryAllocator;
        std::unique_ptr<UploadBufferAllocator> mUploadBufferAllocator;
        std::unique_ptr<CPUDescriptorAllocator> mCPUDescriptorAllocator;

        ComPtr<ID3D12CommandQueue> mCommandQueue;
        ComPtr<ID3D12RootSignature> mRootSignature;

        ComPtr<ID3D12CommandAllocator> mResourceCommandAllocator[FrameResourceCount];
        ComPtr<ID3D12GraphicsCommandList> mResourceCommandList[FrameResourceCount];
        std::shared_ptr<DeviceBackBuffer> mBackBuffers[FrameResourceCount];
        std::shared_ptr<DeviceTexture2D> mDepthStencil;

        std::shared_ptr<DeviceVertexBuffer> mScreenVertexBuffer;
        std::shared_ptr<DeviceIndexBuffer> mScreenIndexBuffer;

        ComPtr<ID3D12Debug> mDebugController;
        ComPtr<IDXGIFactory4> mDxgiFactory;
        ComPtr<ID3D12Device> mDevice;
        ComPtr<IDXGISwapChain3> mSwapChain;
        ComPtr<ID3D12Fence> mFence;

        ShaderResourceView mNullSRV;
        UnorderAccessView mNullUAV;
        RenderTargetView mNullRTV;

        // resources wait to be released
        std::array<std::vector<MemoryAllocation*>, FrameResourceCount> mResourceCache;

        HANDLE mFenceEvent;
        uint32 mFrameIndex;
        uint32 mBackBufferIndex;

        uint32 mFenceValue;
        uint32 mWidth;
        uint32 mHeight;
        bool mResourceInitialized;
    };
}