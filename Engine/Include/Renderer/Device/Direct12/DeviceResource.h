#pragma once
#include <array>
#include "MemoryAllocator.h"
#include "DescriptorAllocator.h"

namespace MRenderer
{
    // same as DXGI_FORMAT
    enum ETextureFormat : uint8
    {
        ETextureFormat_None = 0,
        ETextureFormat_R32G32B32A32_FLOAT = 2,
        ETextureFormat_R16G16B16A16_FLOAT = 10,
        ETextureFormat_R16G16B16A16_UNORM = 11,
        ETextureFormat_FORMAT_R10G10B10A2_UNORM = 24,
        ETextureFormat_R8G8B8A8_UNORM = 28,
        ETextureFormat_R16G16_UNORM = 35,
        ETextureFormat_R8G8_UNORM = 49,
        ETextureFormat_R8_UNORM = 61,
        ETextureFormat_DepthStencil = 100,
    };

    // same as D3D12_FILTER
    enum ESamplerFilter : uint8
    {
        ESamplerFilter_Point = 0,
        ESamplerFilter_Linear = 0x15,
        ESamplerFilter_Anisotropic = 0x55,
    };

    // same as D3D12_TEXTURE_ADDRESS_MODE
    enum ESamplerAddressMode : uint8
    {
        ESamplerAddressMode_Wrap = 1,
        ESamplerAddressMode_Mirror = 2,
        ESamplerAddressMode_Clamp = 3,
        ESamplerAddressMode_Border = 4,
        ESamplerAddressMode_MirrorOnce = 5,
    };

    inline uint32 GetChannelCount(ETextureFormat format) 
    {
        switch (format)
        {
            case MRenderer::ETextureFormat_R16G16B16A16_UNORM:
            case MRenderer::ETextureFormat_R8G8B8A8_UNORM:
                return 4;
            case MRenderer::ETextureFormat_R8G8_UNORM:
                return 2;
            case MRenderer::ETextureFormat_R8_UNORM:
                return 1;
            default:
                return 0;
        }
        return format;
    }

    // for d3d12 resource RAII management, state tracking and keeping mapped pointer
    class D3D12Resource
    {
    public:
        D3D12Resource()
            :mAllocation(nullptr), mResource(nullptr), mResourceState(D3D12_RESOURCE_STATE_COMMON), mMapped(0)
        {
        }

        // for back buffer, @D3D12Resource won't interfere with the resource life cycle
        D3D12Resource(ID3D12Resource* allocation, D3D12_RESOURCE_STATES state, uint8* mapped = nullptr)
            :mAllocation(nullptr), mResource(allocation), mResourceState(state), mMapped(mapped)
        {
        }

        // fow our own resource allocation, @D3D12Resource will manage it's life cycle by RAII
        D3D12Resource(D3D12Memory::MemoryAllocation* allocation, D3D12_RESOURCE_STATES state, uint8* mapped = nullptr)
            :mAllocation(allocation), mResource(allocation->Resource()), mResourceState(state), mMapped(mapped)
        {
        }

        D3D12Resource(const D3D12Resource&) = delete;
        D3D12Resource(D3D12Resource&& other);
        virtual ~D3D12Resource();

        D3D12Resource& operator=(D3D12Resource&& other);

        inline ID3D12Resource* Resource() const
        {
            return mResource;
        }

        inline ETextureFormat Format() const
        {
            return static_cast<ETextureFormat>(Resource()->GetDesc().Format);
        }

        inline D3D12_RESOURCE_STATES ResourceState() const
        {
            return mResourceState;
        }

        inline void* GetMappedPtr() 
        {
            return mMapped;
        }

        void SetName(std::wstring_view name)
        {
            mName = name;
            mResource->SetName(name.data());
        }

        void TransitionBarrier(ID3D12GraphicsCommandList* command_list, D3D12_RESOURCE_STATES state);

        friend void Swap(D3D12Resource& lhs, D3D12Resource& rhs)
        {
            std::swap(lhs.mResource, rhs.mResource);
            std::swap(lhs.mResourceState, rhs.mResourceState);
            std::swap(lhs.mMapped, rhs.mMapped);
            std::swap(lhs.mAllocation, rhs.mAllocation);
        }

    protected:
        MemoryAllocation* mAllocation;
        ID3D12Resource* mResource;
        D3D12_RESOURCE_STATES mResourceState;
        uint8* mMapped;
        std::wstring mName;
    };

    class ResourceView 
    {
    protected:
        ResourceView() 
            :mResource(nullptr)
        {
        }

        ResourceView(D3D12Resource* resource, CPUDescriptor descriptor)
            :mResource(resource), mDescriptor(std::move(descriptor))
        {
            ASSERT(resource);
        }

    public:
        virtual ~ResourceView() = default;

        // support move semantics
        ResourceView(const ResourceView& other) = delete;
        ResourceView(ResourceView&& other)
            :ResourceView()
        {
            Swap(*this, other);
        }
        
        ResourceView& operator=(ResourceView other) 
        {
            Swap(*this, other);
            return *this;
        }

        inline D3D12Resource* Resource() { return mResource; }
        inline CPUDescriptor* Descriptor() { return &mDescriptor; }
        bool Empty() const { return mResource == nullptr; }

        friend void Swap(ResourceView& lhs, ResourceView& rhs) 
        {
            std::swap(lhs.mResource, rhs.mResource);
            Swap(lhs.mDescriptor, rhs.mDescriptor);
        }

    protected:
        D3D12Resource* mResource;
        CPUDescriptor mDescriptor;
    };

    class ShaderResourceView : public ResourceView
    {
    public:
        ShaderResourceView()
            :ResourceView()
        {
        }

        ShaderResourceView(D3D12Resource* resource, CPUDescriptor descriptor)
            :ResourceView(resource, std::move(descriptor))
        {
        }
    };

    class UnorderAccessView : public ResourceView
    {
    public:
        UnorderAccessView() 
            : ResourceView()
        {
        }

        UnorderAccessView(D3D12Resource* resource, CPUDescriptor descriptor)
            :ResourceView(resource, std::move(descriptor))
        {
        }
    };

    class RenderTargetView : public ResourceView 
    {
    public:
        RenderTargetView() 
            : ResourceView()
        {
        }

        RenderTargetView(D3D12Resource* resource, CPUDescriptor descriptor)
            :ResourceView(resource, std::move(descriptor))
        {
        }
    };

    class DepthStencilView : public ResourceView 
    {
    public:
        DepthStencilView()
            : ResourceView()
        {
        }

        DepthStencilView(D3D12Resource* resource, CPUDescriptor descriptor)
            :ResourceView(resource, std::move(descriptor))
        {
        }
    };

    class ConstantBufferView : public ResourceView 
    {
    public:
        ConstantBufferView()
            : ResourceView()
        {
        }

        ConstantBufferView(D3D12Resource* resource, CPUDescriptor descriptor)
            :ResourceView(resource, std::move(descriptor))
        {
        }
    };

    class IDeviceResource
    {

    public:
        virtual ~IDeviceResource() {};
    };

    // base class of texture2d, texture2d array and render target
    // provide some common interface about format, size, and descriptor
    class DeviceTexture : public IDeviceResource
    {
    protected:
        DeviceTexture(D3D12Resource resource)
            :mTextureResource(std::move(resource))
        {
            const auto& desc = mTextureResource.Resource()->GetDesc();

            mWidth = static_cast<uint32>(desc.Width);
            mHeight = static_cast<uint32>(desc.Height);
            mTextureFormat = static_cast<ETextureFormat>(desc.Format);
        }
        DeviceTexture(DeviceTexture&& other) = default;

    public:
        DeviceTexture(const DeviceTexture& other) = delete;

        inline D3D12Resource* Resource() { return &mTextureResource; }
        inline void SetShaderResourceView(ShaderResourceView view) { mShaderResourceView = std::move(view); }
        inline void SetUnorderedAccessView(UnorderAccessView view) { mUnorderedAccessView = std::move(view); }
        inline ShaderResourceView* GetShaderResourceView() { ASSERT(!mShaderResourceView.Empty()); return &mShaderResourceView; }
        inline UnorderAccessView* GetUnorderedResourceView() { ASSERT(!mUnorderedAccessView.Empty());  return &mUnorderedAccessView; }
        inline const ETextureFormat Format() const { return mTextureFormat; }
        inline uint32 TextureWidth() const { return mWidth; }
        inline uint32 TextureHeight() const { return mHeight; }

    protected:
        D3D12Resource mTextureResource;
        ShaderResourceView mShaderResourceView;
        UnorderAccessView mUnorderedAccessView;
        uint32 mWidth, mHeight;
        ETextureFormat mTextureFormat;
    };

    class DeviceTexture2D : public DeviceTexture
    {
    public:
        DeviceTexture2D(D3D12Resource resource) 
            : DeviceTexture(std::move(resource))
        {
        }
        DeviceTexture2D(DeviceTexture2D&& other) = default;
    };

    class DeviceTexture2DArray : public DeviceTexture
    {
    public:
        DeviceTexture2DArray(D3D12Resource resource, uint32 mip_size);
        DeviceTexture2DArray(DeviceTexture2DArray&& other) = default;

        void UpdateArraySlice(uint32 index, const void* data, uint32 size);

        inline const uint32 MipSize() const { return mMipSize; }
        inline UnorderAccessView* GetUnorderedAccessView(uint32 index) { ASSERT(index < MipSize() && !mUnorderedAccessViewArray[index].Empty()); return &mUnorderedAccessViewArray[index]; }
        inline void SetUnorderedAccessView(int32 index, UnorderAccessView uav) { mUnorderedAccessViewArray[index] = std::move(uav); }

    private:
        using DeviceTexture::SetUnorderedAccessView;
        using DeviceTexture::GetShaderResourceView;

    protected:
        // each descriptor is a mip slice of texture2d array
        std::vector<UnorderAccessView> mUnorderedAccessViewArray;
        uint32 mSliceSize;
        uint32 mMipSize;
    };

    class DeviceRenderTarget : public DeviceTexture2D
    {
    public:
        DeviceRenderTarget(D3D12Resource resource)
            :DeviceTexture2D(std::move(resource))
        {
        }
        DeviceRenderTarget(DeviceRenderTarget&& other) = default;

        inline RenderTargetView* GetRenderTargetView() { ASSERT(!mRenderTargetView.Empty());  return &mRenderTargetView; }
        inline void SetRenderTargetView(RenderTargetView view) { mRenderTargetView = std::move(view); }

    protected:
        RenderTargetView mRenderTargetView;
    };

    class DeviceDepthStencil : public DeviceTexture
    {
    public:
        DeviceDepthStencil(D3D12Resource resource)
            :DeviceTexture(std::move(resource))
        {
        }
        DeviceDepthStencil(DeviceDepthStencil&& other) = default;

        inline DepthStencilView* GetDepthStencilView() { ASSERT(!mDepthStencilView.Empty()); return &mDepthStencilView; }
        inline void SetDepthStencilView(DepthStencilView view) { mDepthStencilView = std::move(view); }

    protected:
        DepthStencilView mDepthStencilView;
    };

    class DeviceStructuredBuffer : public IDeviceResource
    {
    public:
        DeviceStructuredBuffer(D3D12Resource resource) 
            :mBuffer(std::move(resource))
        {
        }
        DeviceStructuredBuffer(DeviceStructuredBuffer&& other) = default;

        inline D3D12Resource* Resource() { return &mBuffer; }
        inline ShaderResourceView* GetShaderResourceView() { ASSERT(!mShaderResourceView.Empty()); return &mShaderResourceView; }
        inline UnorderAccessView* GetUnorderedAccessView() { ASSERT(!mUnorderedResourceView.Empty()); return &mUnorderedResourceView; }
        inline void SetShaderResourceView(ShaderResourceView view) { mShaderResourceView = std::move(view); }
        inline void SetUnorderedResourceView(UnorderAccessView view) { mUnorderedResourceView = std::move(view); }

        void Commit(const void* data, uint32 size);

    protected:
        D3D12Resource mBuffer;
        ShaderResourceView mShaderResourceView;
        UnorderAccessView mUnorderedResourceView;
    };


    class DeviceVertexBuffer : public IDeviceResource
    {
    public:
        DeviceVertexBuffer(D3D12Resource resource, D3D12_VERTEX_BUFFER_VIEW vbv)
            :mVertexBuffer(std::move(resource)), mView(vbv)
        {
        }
        DeviceVertexBuffer(DeviceVertexBuffer&& other) = default;

        inline const D3D12_VERTEX_BUFFER_VIEW& VertexBufferView() const
        {
            return mView;
        }

        inline uint32 VertexCount() const
        {
            return mView.SizeInBytes / mView.StrideInBytes;
        }

        inline uint32 VertexStride() const
        {
            return mView.StrideInBytes;
        }

    protected:
        D3D12Resource mVertexBuffer;
        D3D12_VERTEX_BUFFER_VIEW mView;
    };


    class DeviceIndexBuffer : public IDeviceResource
    {
    public:
        DeviceIndexBuffer(D3D12Resource resource, D3D12_INDEX_BUFFER_VIEW ibv)
            :mIndexBuffer(std::move(resource)), mView(ibv)
        {
        }
        DeviceIndexBuffer(DeviceIndexBuffer&& other) = default;

        inline const D3D12_INDEX_BUFFER_VIEW& IndexBufferView() const
        {
            return mView;
        }

        inline const uint32 IndiciesCount() const
        {
            ASSERT(mView.Format == DXGI_FORMAT_R32_UINT);
            return mView.SizeInBytes / sizeof(uint32);
        }

    protected:
        D3D12Resource mIndexBuffer;
        D3D12_INDEX_BUFFER_VIEW mView;
    };


    class DeviceConstantBuffer : protected IDeviceResource
    {
    public:
        DeviceConstantBuffer(std::array<D3D12Resource, FrameResourceCount> resource_array, uint32 buffer_size)
            :mBufferArray(std::move(resource_array)), mBufferSize(buffer_size)
        {
        }
        DeviceConstantBuffer(DeviceConstantBuffer&& other) = default;

        inline D3D12Resource* Resource(uint32 index) { return &mBufferArray[index]; }
        inline D3D12Resource* GetCurrentResource();
        ConstantBufferView* GetCurrendConstantBufferView();
        void SetConstantBufferView(std::array<ConstantBufferView, FrameResourceCount> view_array);
        inline uint32 BufferSize() const{ return mBufferSize;}

        template<typename T>
        void CommitData(const T& cbuffer)
        {
            CommitData(static_cast<const void*>(&cbuffer), sizeof(T));
        }
        void CommitData(const void* data, uint32 size);

    protected:
        std::array<D3D12Resource, FrameResourceCount> mBufferArray;
        std::array<ConstantBufferView, FrameResourceCount> mConstantBufferViewArray;
        uint32 mBufferSize;
    };

    class DeviceSampler : public IDeviceResource
    {
    public:
        DeviceSampler(CPUDescriptor cpu_descriptor)
            :mCPUDescriptor(std::move(cpu_descriptor))
        {

        }
        DeviceSampler(DeviceSampler&& other) = default;

        CPUDescriptor* Descriptor() { return &mCPUDescriptor;}

    protected:
        CPUDescriptor mCPUDescriptor;
    };

    class DeviceBackBuffer : public DeviceTexture
    {
    public:
        DeviceBackBuffer(D3D12Resource resource)
            :DeviceTexture(std::move(resource))
        {
        }
        DeviceBackBuffer(DeviceBackBuffer&& other) = default;

        inline RenderTargetView* GetRenderTargetView() { ASSERT(!mRenderTargetView.Empty()); return &mRenderTargetView; }
        inline void SetRenderTargetView(RenderTargetView view) { mRenderTargetView = std::move(view); }

    protected:
        using DeviceTexture::GetShaderResourceView;
        using DeviceTexture::GetUnorderedResourceView;
        using DeviceTexture::SetShaderResourceView;
        using DeviceTexture::SetUnorderedAccessView;

    protected:
        D3D12Resource mResource;
        RenderTargetView mRenderTargetView;
    };

    constexpr uint32 MaxRenderTargets = 8;

    enum EBlendFactor : uint8
    {
        EBlendFactor_Zero = 1,
        EBlendFactor_One = 2,
        EBlendFactor_SrcAlpha = 5,
        EBlendFactor_InvSrcAlpha = 6,
        EBlendFactor_DestAlpha = 7,
        EBlendFactor_InvDestAlpha = 8,
    };

    enum EBlendOperation : uint8
    {
        EBlendOperation_Add = 1,
        EBlendOperation_Subtract = 2,
        EBlendOperation_RevSubtract = 3,
        EBlendOperation_Min = 4,
        EBlendOperation_Max = 5,
    };


    struct PipelineBlendStateDesc
    {
        bool EnableBlend : 4;
        EBlendOperation BlendOP : 4;
        EBlendFactor SrcFactor : 4;
        EBlendFactor DestFactor : 4;

        inline static PipelineBlendStateDesc None() 
        {
            return PipelineBlendStateDesc{
                .EnableBlend = false,
                .BlendOP = EBlendOperation_Add,
                .SrcFactor = EBlendFactor_Zero,
                .DestFactor = EBlendFactor_One,
            };
        }
    };

    static_assert(sizeof(PipelineBlendStateDesc) == 2);

    enum EFillMode : uint8
    {
        EFillMode_Wireframe = 2,
        EFillMode_Solid = 3,
    };

    enum ECullMode : uint8
    {
        ECullMode_None = 1,
        ECullMode_Front = 2,
        ECullMode_Back = 3,
    };

    struct PipelineRasterizerDesc
    {
        EFillMode FillMode;
        ECullMode CullMode;
    };

    enum ECompareFunction : uint8
    {
        ECompareFunction_Never = 1,
        ECompareFunction_Less = 2,
        ECompareFunction_Equal = 3,
        ECompareFunction_LessEqual = 4,
        ECompareFunction_Greater = 5,
        ECompareFunction_NotEqual = 6,
        ECompareFunction_GreaterEqua = 7,
        ECompareFunction_Always = 8
    };

    enum EStencilOperation : uint8
    {
        EStencilOperation_Keep = 1,
        EStencilOperation_Zero = 2,
        EStencilOperation_Replace = 3,
        EStencilOperation_IncreaseSAT = 4,
        EStencilOperation_DecreaseSAT = 5,
        EStencilOperation_Invert = 6,
        EStencilOperation_Increase = 7,
        EStencilOperation_Decrease = 8
    };

    struct StencilTestDesc
    {
        ECompareFunction StencilCompareFunc : 4;
        EStencilOperation StencilDepthPassOP : 4;
        EStencilOperation StencilPassDepthFailOP : 4;
        EStencilOperation StencilFailOP : 4;

        inline static StencilTestDesc None()
        {
            return StencilTestDesc{
                .StencilCompareFunc = ECompareFunction_Never,
                .StencilDepthPassOP = EStencilOperation_Keep,
                .StencilPassDepthFailOP = EStencilOperation_Keep,
                .StencilFailOP = EStencilOperation_Keep,
            };
        }

        inline static StencilTestDesc Compare(ECompareFunction func) 
        {
            return StencilTestDesc{
                .StencilCompareFunc = func,
                .StencilDepthPassOP = EStencilOperation_Keep,
                .StencilPassDepthFailOP = EStencilOperation_Keep,
                .StencilFailOP = EStencilOperation_Keep,
            };
        }
    };

    static_assert(sizeof(StencilTestDesc) == 2);

    struct alignas(4) PipelineStateDesc
    {
        // 0 bytes
        // rasterizer state
        EFillMode FillMode : 2;
        ECullMode CullMode : 2;

        // depth stencil state
        bool DepthTestEnable : 1;
        bool DepthWriteEnable : 1;
        bool StencilTestEnable : 1;
        bool StencilWriteEnable: 1;

        // 1 bytes
        ECompareFunction DepthCompareFunc : 4;
        // 2 bytes
        StencilTestDesc FrontFaceStencilDesc;
        StencilTestDesc BackFaceStencilDesc;
        // 6 bytes

        // blend state
        PipelineBlendStateDesc BlendState;
        // 8 bytes

        inline static PipelineStateDesc Generate(bool depth_test, bool depth_write, ECullMode cull_model)
        {
            return PipelineStateDesc{
                .FillMode = EFillMode_Solid,
                .CullMode = cull_model,
                .DepthTestEnable = depth_test,
                .DepthWriteEnable = depth_write,
                .StencilTestEnable = false,
                .DepthCompareFunc = ECompareFunction_Less,
                .FrontFaceStencilDesc = StencilTestDesc::None(),
                .BackFaceStencilDesc = StencilTestDesc::None(),
                .BlendState = PipelineBlendStateDesc::None(),
            };
        }

        inline static PipelineStateDesc DefaultOpaque() 
        {
            return PipelineStateDesc{
                .FillMode = EFillMode_Solid,
                .CullMode = ECullMode_Back,
                .DepthTestEnable = true,
                .DepthWriteEnable = true,
                .StencilTestEnable = false,
                .DepthCompareFunc = ECompareFunction_Less,
                .FrontFaceStencilDesc = StencilTestDesc::None(),
                .BackFaceStencilDesc = StencilTestDesc::None(),
                .BlendState = PipelineBlendStateDesc::None(),
            };
        }

        inline static PipelineStateDesc DrawScreen() 
        {
            return PipelineStateDesc{
                .FillMode = EFillMode_Solid,
                .CullMode = ECullMode_None,
                .DepthTestEnable = false,
                .DepthWriteEnable = false,
                .StencilTestEnable = false,
                .DepthCompareFunc = ECompareFunction_Always,
                .FrontFaceStencilDesc = StencilTestDesc::None(),
                .BackFaceStencilDesc = StencilTestDesc::None(),
                .BlendState = PipelineBlendStateDesc::None(),
            };
        }
    };

    static_assert(sizeof(PipelineStateDesc) == 8);

    struct RenderPassStateDesc 
    {
        // formate of depth stencil
        ETextureFormat DepthStencilFormat;

        // numbers of render target
        uint8 NumRenderTarget;

        // format of render targets
        ETextureFormat RenderTargetFormats[MaxRenderTargets];
    };

    static_assert(sizeof(RenderPassStateDesc) == 10);

    struct ResourceBinding 
    {
        // shader input resource
        std::array<ShaderResourceView*, ShaderResourceMaxTexture> SRVs = {};

        // compute shader output resource
        std::array<UnorderAccessView*, ShaderResourceMaxUAV> UAVs = {};
    }; 

    }