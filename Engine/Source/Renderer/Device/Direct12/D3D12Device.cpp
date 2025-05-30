#include <vector>
#include <type_traits>

#include "DirectXTex.h"
#include "Renderer/Device/Direct12/D3D12Device.h"
#include "Renderer/Device/Direct12/D3DUtils.h"
#include "App.h"
#include "Fundation.h"
#include "Renderer/Pipeline/IPipeline.h"
#include "Resource/DefaultResource.h"
#include "Resource/ResourceDef.h"


namespace MRenderer
{
    ID3D12Device* GD3D12RawDevice = nullptr;
    D3D12Device* GD3D12Device = nullptr;

    D3D12Device::D3D12Device(uint32 width, uint32 height)
        : mViewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
          mScissorRect(0, 0, width, height), mFenceValue(1),
          mWidth(width), mHeight(height), mResourceInitialized(false)
    {
#ifndef NDebug
        //ComPtr, & operator will release the reference, getaddressof won't
        // debug controller
        ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&mDebugController)));
        mDebugController->EnableDebugLayer();
#endif

        // dxgi factory
        ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&mDxgiFactory)));

        // device
        ThrowIfFailed(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&mDevice)));

        //TODO CheckFeatureSupport
        // command queue
        D3D12_COMMAND_QUEUE_DESC queue_desc
        {
            .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
            .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE
        };
        ThrowIfFailed(mDevice->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&mCommandQueue)));

        // fence for notification for presenting
        ThrowIfFailed(mDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence)));

        // swap chain
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferDesc.Width = mWidth;
        sd.BufferDesc.Height = mHeight;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.BufferDesc.Format = BackBufferFormat;
        sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
        sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = FrameResourceCount;
        sd.OutputWindow = App::GetApp()->MainWnd();
        sd.Windowed = true;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        // Note: Swap chain uses queue to perform flush.

        ComPtr<IDXGISwapChain> swap_chain;
        ThrowIfFailed(mDxgiFactory->CreateSwapChain(mCommandQueue.Get(), &sd, &swap_chain));
        swap_chain.As<IDXGISwapChain3>(&mSwapChain);

        // root signature
        mRootSignature = CreateRootSignature();

        // resource allocator
        mMemoryAllocator = std::make_unique<MemoryAllocator>(mDevice.Get());
        mUploadBufferAllocator = std::make_unique<UploadBufferAllocator>(mDevice.Get());

        mCPUDescriptorAllocator = std::make_unique<CPUDescriptorAllocator>(mDevice.Get());

        // frame resources
        mBackBufferIndex = mSwapChain->GetCurrentBackBufferIndex();
        mFrameIndex = 0;

        for (uint32 i = 0; i < FrameResourceCount; i++)
        {
            // collect back buffer
            ID3D12Resource* render_target;
            mSwapChain->GetBuffer(i, IID_PPV_ARGS(&render_target));
            render_target->SetName((std::wstring(L"BackBuffer_") + std::to_wstring(mFrameIndex)).data());
            auto& resource = mBackBuffers[i] = std::make_unique<DeviceBackBuffer>(D3D12Resource(render_target, D3D12_RESOURCE_STATE_PRESENT, nullptr));
            resource->SetRenderTargetView(CreateRenderTargetView(nullptr, resource->Resource()));

            // create command list
            ID3D12GraphicsCommandList* resource_command_list;
            ID3D12CommandAllocator* resource_command_allocator;
            ThrowIfFailed(mDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&resource_command_allocator)));
            ThrowIfFailed(mDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, resource_command_allocator, nullptr, IID_PPV_ARGS(&resource_command_list)));

            ThrowIfFailed(resource_command_list->Close());

            mResourceCommandList[i] = resource_command_list;
            mResourceCommandAllocator[i] = resource_command_allocator;
        }

        // null descriptor
        static D3D12Resource null_resource;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = 
        {
            .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
            .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Texture2D = 
            {
                .MostDetailedMip = 0,
                .MipLevels = 1,
            }
        };
        mNullSRV = CreateShaderResourceView(&srv_desc, &null_resource);

        D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = 
        {
            .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
            .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D,
            .Texture2D = 
            {
                .MipSlice = 0,
            }
        };
        mNullUAV = CreateUnorderedAccessView(&uav_desc, &null_resource);

        D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = 
        {
            .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
            .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
            .Texture2D = 
            {
                .MipSlice = 0,
            }
        };
        mNullRTV = CreateRenderTargetView(&rtv_desc, &null_resource);

        // windows event
        mFenceEvent = CreateEvent(nullptr, false, false, nullptr);

        ASSERT(GD3D12RawDevice == nullptr);
        ASSERT(GD3D12Device == nullptr);

        GD3D12RawDevice = mDevice.Get();
        GD3D12Device = this;

#ifndef NDebug
        //LogAdapters();
#endif
    }

    D3D12Device::~D3D12Device()
    {
        GD3D12Device = nullptr;
        GD3D12RawDevice = nullptr;
    }

    // initialize internal resource
    void D3D12Device::InitializeInternalResource()
    {
        ASSERT(!mResourceInitialized);
        mResourceInitialized = true;

        // create screen mesh
        // near plane vertex coordinates in NDC space
        // something like this

        // 1 | ^
        //   |   ^
        //   |     ^
        //   |_______^
        //   |       | ^
        //   |       |   ^
        //   |       |     ^
        // 0 |_______|_______^ 2

        const Vertex<EVertexFormat_P3F_T2F> screen_vertices [] = {
            {{-1, -1, 0}, {0, 1}},
            {{-1,  3, 0}, {0, -1}},
            {{ 3,  -1, 0}, {2, 1}},
        };

        const uint32 screen_indicies [] =
        {
            0,1,2
        };

        mScreenVertexBuffer = CreateVertexBuffer(screen_vertices, sizeof(screen_vertices), sizeof(Vertex<EVertexFormat_P3F_T2F>));
        mScreenIndexBuffer = CreateIndexBuffer(screen_indicies, sizeof(screen_indicies));
    }

    void D3D12Device::LogAdapters()
    {
        UINT i = 0;
        IDXGIAdapter* adapter = nullptr;
        std::vector<IDXGIAdapter*> adapterList;
        while (mDxgiFactory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND)
        {
            DXGI_ADAPTER_DESC desc;
            adapter->GetDesc(&desc);

            std::wstring text = L"***Adapter: ";
            text += desc.Description;
            text += L"\n";

            Log(text);

            adapterList.push_back(adapter);

            ++i;
        }

        for (size_t i = 0; i < adapterList.size(); ++i)
        {
            LogAdapterOutputs(adapterList[i]);
            ReleaseCom(adapterList[i]);
        }
    }
    
    std::shared_ptr<DeviceVertexBuffer> D3D12Device::CreateVertexBuffer(const void* data, uint32 data_size, uint32 stride)
    {
        ASSERT(data_size % stride == 0);

        const D3D12_RESOURCE_STATES InitialState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        MemoryAllocation* allocation = CreateDeviceBuffer(data_size, false, data, InitialState);

        // create vertex buffer view
        D3D12_VERTEX_BUFFER_VIEW vbv = {};
        vbv.BufferLocation = allocation->Resource()->GetGPUVirtualAddress();
        vbv.SizeInBytes = data_size;
        vbv.StrideInBytes = stride;

        return std::make_shared<DeviceVertexBuffer>(D3D12Resource(allocation, InitialState, nullptr), vbv);
    }

    std::shared_ptr<DeviceIndexBuffer> D3D12Device::CreateIndexBuffer(const uint32* data, uint32 data_size)
    {  
        ASSERT(data_size % sizeof(IndexType) == 0);

        const D3D12_RESOURCE_STATES InitialState = D3D12_RESOURCE_STATE_INDEX_BUFFER;
        MemoryAllocation* allocation = CreateDeviceBuffer(data_size, false, data, InitialState);

        // create index buffer view
        D3D12_INDEX_BUFFER_VIEW ibv = {};
        ibv.BufferLocation = allocation->Resource()->GetGPUVirtualAddress();
        ibv.SizeInBytes = data_size;
        ibv.Format = DXGI_FORMAT_R32_UINT; // index format is fixed to DXGI_FORMAT_R32_UINT
        
        return std::make_shared<DeviceIndexBuffer>(D3D12Resource(allocation, InitialState, nullptr), ibv);
    }

    std::shared_ptr<DeviceStructuredBuffer> D3D12Device::CreateStructuredBuffer(uint32 data_size , uint32 stride, const void* initial_data/*=nullptr*/)
    {
        ASSERT(data_size % stride == 0);
        ASSERT(stride % 4);

        // resource allocation
        MemoryAllocation* allocation = CreateDeviceBuffer(data_size, true, initial_data, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        auto resource = std::make_shared<DeviceStructuredBuffer>(D3D12Resource(allocation, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr));

        // srv descriptor
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc
        {
            .Format = DXGI_FORMAT_UNKNOWN,
            .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
            .Buffer
            {
                .FirstElement = 0,
                .NumElements = data_size / stride,
                .StructureByteStride = stride,
                .Flags = D3D12_BUFFER_SRV_FLAG_NONE
            }
        };
        resource->SetShaderResourceView(CreateShaderResourceView(&srv_desc, resource->Resource()));

        // uav descriptor
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc
        {
            .Format = DXGI_FORMAT_UNKNOWN,
            .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
            .Buffer
            {
                .FirstElement = 0,
                .NumElements = data_size / stride,
                .StructureByteStride = stride,
                .Flags = D3D12_BUFFER_UAV_FLAG_NONE
            }
        };
        resource->SetUnorderedResourceView(CreateUnorderedAccessView(&uav_desc, resource->Resource()));

        return resource;
    }

    std::shared_ptr<DeviceTexture2D> D3D12Device::CreateTexture2D(uint32 width, uint32 height, ETextureFormat format, const void* initial_data/*=nullptr*/, bool unorder_access/*=false*/ )
    {
        ASSERT(format != ETextureFormat_DepthStencil);
        DXGI_FORMAT dxgi_format = static_cast<DXGI_FORMAT>(format);

        D3D12_RESOURCE_STATES res_state = initial_data ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_COMMON;

        // allocate 2d texture
        AllocationDesc allocation_desc = {
            .resource_desc = CD3DX12_RESOURCE_DESC::Tex2D(dxgi_format, width, height, 1, 0, 1, 0, unorder_access ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE),
            .heap_type = D3D12_HEAP_TYPE_DEFAULT,
            .initial_state = res_state,
        };

        MemoryAllocation* allocation = mMemoryAllocator->Allocate(allocation_desc);
        auto resource = std::make_shared<DeviceTexture2D>(D3D12Resource(allocation->Resource(), res_state, nullptr));

        // upload initial_data to gpu if it's provided
        if (initial_data) 
        {
            uint32 pixel_size = static_cast<uint32>(DirectX::BitsPerPixel(dxgi_format) / CHAR_BIT);

            // allocate upload buffer
            size_t intermediate_size = GetRequiredIntermediateSize(allocation->Resource(), 0, 1);
            UploadBuffer upload_buffer = mUploadBufferAllocator->Allocate(static_cast<uint32>(intermediate_size));
        
            D3D12_SUBRESOURCE_DATA resource{
                .pData = initial_data,
                .RowPitch = static_cast<LONG_PTR>(width * pixel_size),
                .SlicePitch = static_cast<LONG_PTR>(width * height * pixel_size),
            };

            // copy texture data
            ID3D12GraphicsCommandList* command_list = mResourceCommandList[mFrameIndex].Get();
            UpdateSubresources(command_list, allocation->Resource(), upload_buffer.resource, upload_buffer.offset, 0, 1, &resource);
        }

        // srv descriptor
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc
        {
            .Format = dxgi_format,
            .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Texture2D{
                .MipLevels = 1
            }
        };
        resource->SetShaderResourceView(CreateShaderResourceView(&srv_desc, resource->Resource()));

        // uav descriptor
        if (unorder_access) 
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc
            {
                .Format = dxgi_format,
                .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D,
                .Texture2D = {}
            };
            resource->SetUnorderedAccessView(CreateUnorderedAccessView(&uav_desc, resource->Resource()));
        }
        return resource;
    }


    std::shared_ptr<DeviceTexture2DArray> D3D12Device::CreateTextureCube(uint32 width, uint32 height, uint32 mip_level, ETextureFormat format, std::array<TextureData, 6>* initial_data, bool unorder_access)
    {
        DXGI_FORMAT dxgi_format = static_cast<DXGI_FORMAT>(format);

        // allocate 2d texture arrray
        AllocationDesc allocation_desc = {
            .resource_desc = CD3DX12_RESOURCE_DESC::Tex2D(dxgi_format, width, height, 6, mip_level, 1, 0, unorder_access ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE),
            .heap_type = D3D12_HEAP_TYPE_DEFAULT,
            .initial_state = D3D12_RESOURCE_STATE_COPY_DEST,
        };

        MemoryAllocation* allocation = mMemoryAllocator->Allocate(allocation_desc);
        auto resource = std::make_shared<DeviceTexture2DArray>(D3D12Resource(allocation, D3D12_RESOURCE_STATE_COPY_DEST, nullptr), mip_level);

        // generate srv descriptor
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc
        {
            .Format = dxgi_format,
            .ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE,
            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Texture2DArray{
                .MipLevels = UINT32_MAX,
                .FirstArraySlice = 0,
                .ArraySize = 6,
            }
        };
        resource->SetShaderResourceView(CreateShaderResourceView(&srv_desc, resource->Resource()));

        if (unorder_access) 
        {
            ASSERT(mip_level > 0);
            for (uint32 i = 0; i < mip_level; i++) 
            {
                // uav descriptor
                D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc
                {
                    .Format = dxgi_format,
                    .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY,
                    .Texture2DArray = 
                    {
                        .MipSlice = i,
                        .FirstArraySlice = 0,
                        .ArraySize = 6,
                        .PlaneSlice = 0,
                    }
                };
                resource->SetUnorderedAccessView(i, CreateUnorderedAccessView(&uav_desc, resource->Resource()));
            }
        }

        if (initial_data) 
        {
            for (uint32 i = 0; i < min(initial_data->size(), 6); i++)
            {
                resource->UpdateArraySlice(i, initial_data->at(i).mData.GetData(), initial_data->at(i).mData.GetSize());
            }
        }
        return resource;
    }

    std::shared_ptr<DeviceConstantBuffer> D3D12Device::CreateConstBuffer(uint32 buffer_size)
    {
        // must be a multiple of 256 bytes
        buffer_size = buffer_size + (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1) & ~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1);
        buffer_size = max(buffer_size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

        // allocate constant buffer
        // all constant buffer resides in upload heap for now
        AllocationDesc allocation_desc = {
            .resource_desc = CD3DX12_RESOURCE_DESC::Buffer(buffer_size),
            .heap_type = D3D12_HEAP_TYPE_UPLOAD,
            .initial_state = D3D12_RESOURCE_STATE_GENERIC_READ,
        };

        std::array<D3D12Resource, FrameResourceCount> resource_array;
        for (uint32 i = 0; i < FrameResourceCount; i++)
        {
            // allocate resource
            MemoryAllocation* allocation = mMemoryAllocator->Allocate(allocation_desc);

            // persistent mapping
            void* mapped = nullptr;
            CD3DX12_RANGE readRange(0, 0); // we don't need to read this buffer from cpu side
            ThrowIfFailed(allocation->Resource()->Map(0, &readRange, &mapped));
            resource_array[i] = D3D12Resource(allocation, D3D12_RESOURCE_STATE_GENERIC_READ, static_cast<uint8*>(mapped));
        }
        auto cbuffer = std::make_shared<DeviceConstantBuffer>(std::move(resource_array), buffer_size);

        // generate cbv
        std::array<ConstantBufferView, FrameResourceCount> cbv_array;
        for (uint32 i = 0; i < FrameResourceCount; i++)
        {
            D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc;
            cbv_desc.SizeInBytes = buffer_size;
            cbv_desc.BufferLocation = cbuffer->Resource(i)->Resource()->GetGPUVirtualAddress();

            cbv_array[i] = CreateConstantBufferView(&cbv_desc, cbuffer->Resource(i));
        }
        cbuffer->SetConstantBufferView(std::move(cbv_array));

        return cbuffer;
    }

    std::shared_ptr<DeviceRenderTarget> D3D12Device::CreateRenderTarget(uint32 width, uint32 height, ETextureFormat format, D3D12_RESOURCE_STATES state)
    {
        // resource allocation
        DXGI_FORMAT dxgi_format = static_cast<DXGI_FORMAT>(format);
        AllocationDesc allocation_desc = {
            .resource_desc = CD3DX12_RESOURCE_DESC::Tex2D(dxgi_format, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
            .heap_type = D3D12_HEAP_TYPE_DEFAULT,
            .initial_state = state,
            .defalut_value = D3D12_CLEAR_VALUE{.Format = dxgi_format, .Color = {0, 0, 0, 0}}
        };

        MemoryAllocation* allocation = mMemoryAllocator->Allocate(allocation_desc);
        auto resource = std::make_shared<DeviceRenderTarget>(D3D12Resource(allocation, state, nullptr));

        // generate rtv
        D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {
            .Format = dxgi_format,
            .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
        };
        resource->SetRenderTargetView(CreateRenderTargetView(&rtv_desc, resource->Resource()));

        // generate srv
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc
        {
            .Format = dxgi_format,
            .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Texture2D{
                .MipLevels = 1
            }
        };
        resource->SetShaderResourceView(CreateShaderResourceView(&srv_desc, resource->Resource()));
        return resource;
    }

    std::shared_ptr<DeviceDepthStencil> D3D12Device::CreateDepthStencil(uint32 width, uint32 height, D3D12_RESOURCE_STATES state)
    {
        AllocationDesc allocation_desc = {
            .resource_desc = CD3DX12_RESOURCE_DESC::Tex2D(DepthStencilFormat, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
            .heap_type = D3D12_HEAP_TYPE_DEFAULT,
            .initial_state = state,
            .defalut_value = D3D12_CLEAR_VALUE{.Format = DepthStencilFormat, .DepthStencil = D3D12_DEPTH_STENCIL_VALUE{1.0, 0}}
        };

        MemoryAllocation* allocation = mMemoryAllocator->Allocate(allocation_desc);
        auto resource = std::make_shared<DeviceDepthStencil>(D3D12Resource(allocation, state, nullptr));

        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc =
        {
            .Format = DepthStencilSRVFormat,
            .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Texture2D = 
            {
                .MipLevels = 1,
            },
        };
        resource->SetShaderResourceView(CreateShaderResourceView(&srv_desc, resource->Resource()));

        D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = 
        {
            .Format = DepthStencilFormat,
            .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
            .Texture2D = {
                .MipSlice = 0,
            }
        };
        resource->SetDepthStencilView(CreateDepthStencilView(&dsv_desc, resource->Resource()));

        return resource;
    }

    std::shared_ptr<PipelineStateObject> D3D12Device::CreateGraphicsPipelineStateObject(EVertexFormat format, const PipelineStateDesc* pipeline_desc, const RenderPassStateDesc* pass_desc, const D3D12ShaderProgram* program)
    {
        IDxcBlob* vs = program->mVS->GetShaderByteCode();
        IDxcBlob* ps = program->mPS->GetShaderByteCode();

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};

        // vertex layout
        VertexDefination vertex_def = GetVertexLayout(format);

        pso_desc.InputLayout = D3D12_INPUT_LAYOUT_DESC{
            vertex_def.VertexLayout,
            vertex_def.NumVertexElements,
        };

        // root signature
        pso_desc.pRootSignature = GD3D12Device->mRootSignature.Get();

        // shader
        pso_desc.VS = CD3DX12_SHADER_BYTECODE(vs->GetBufferPointer(), vs->GetBufferSize());
        pso_desc.PS = CD3DX12_SHADER_BYTECODE(ps->GetBufferPointer(), ps->GetBufferSize());

        // blend state
        pso_desc.BlendState.IndependentBlendEnable = false;
        pso_desc.BlendState.AlphaToCoverageEnable = false;
        pso_desc.BlendState.RenderTarget[0] = {
            .BlendEnable    = pipeline_desc->BlendState.EnableBlend,
            .LogicOpEnable  = false,
            .SrcBlend       = static_cast<D3D12_BLEND>(pipeline_desc->BlendState.SrcFactor),
            .DestBlend      = static_cast<D3D12_BLEND>(pipeline_desc->BlendState.DestFactor),
            .BlendOp        = static_cast<D3D12_BLEND_OP>(pipeline_desc->BlendState.BlendOP),
            .SrcBlendAlpha  = static_cast<D3D12_BLEND>(pipeline_desc->BlendState.SrcFactor),
            .DestBlendAlpha = static_cast<D3D12_BLEND>(pipeline_desc->BlendState.DestFactor),
            .BlendOpAlpha   = static_cast<D3D12_BLEND_OP>(pipeline_desc->BlendState.BlendOP),
            .LogicOp = D3D12_LOGIC_OP_NOOP,
            .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL,
        };

        // rasterizer state
        pso_desc.RasterizerState =
        {
            .FillMode               = static_cast<D3D12_FILL_MODE>(pipeline_desc->FillMode),
            .CullMode               = static_cast<D3D12_CULL_MODE>(pipeline_desc->CullMode),
            .FrontCounterClockwise  = FALSE,
            .DepthBias              = D3D12_DEFAULT_DEPTH_BIAS,
            .DepthBiasClamp         = D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
            .SlopeScaledDepthBias   = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
            .DepthClipEnable        = TRUE,
            .MultisampleEnable      = FALSE,
            .AntialiasedLineEnable  = FALSE,
            .ForcedSampleCount      = 0,
            .ConservativeRaster     = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF,
        };

        // depth stencil state
        pso_desc.DepthStencilState =
        {
            .DepthEnable        = pipeline_desc->DepthTestEnable,
            .DepthWriteMask     = pipeline_desc->DepthWriteEnable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO,
            .DepthFunc          = static_cast<D3D12_COMPARISON_FUNC>(pipeline_desc->DepthCompareFunc),
            .StencilEnable      = pipeline_desc->StencilTestEnable,
            .StencilReadMask    = UINT8_MAX,
            .StencilWriteMask   = pipeline_desc->StencilWriteEnable ? UINT8_MAX : 0ui8,
            .FrontFace =
            {
                .StencilFailOp      = static_cast<D3D12_STENCIL_OP>(pipeline_desc->FrontFaceStencilDesc.StencilFailOP),
                .StencilDepthFailOp = static_cast<D3D12_STENCIL_OP>(pipeline_desc->FrontFaceStencilDesc.StencilPassDepthFailOP),
                .StencilPassOp      = static_cast<D3D12_STENCIL_OP>(pipeline_desc->FrontFaceStencilDesc.StencilDepthPassOP),
                .StencilFunc        = static_cast<D3D12_COMPARISON_FUNC>(pipeline_desc->FrontFaceStencilDesc.StencilCompareFunc),
            },
            .BackFace = 
            {
                .StencilFailOp      = static_cast<D3D12_STENCIL_OP>(pipeline_desc->BackFaceStencilDesc.StencilFailOP),
                .StencilDepthFailOp = static_cast<D3D12_STENCIL_OP>(pipeline_desc->BackFaceStencilDesc.StencilPassDepthFailOP),
                .StencilPassOp      = static_cast<D3D12_STENCIL_OP>(pipeline_desc->BackFaceStencilDesc.StencilDepthPassOP),
                .StencilFunc        = static_cast<D3D12_COMPARISON_FUNC>(pipeline_desc->BackFaceStencilDesc.StencilCompareFunc),
            }
        };
        
        // render_target
        pso_desc.DSVFormat = DepthStencilFormat;
        pso_desc.NumRenderTargets = pass_desc->NumRenderTarget;
        for (uint32 i = 0; i < pso_desc.NumRenderTargets; i++) 
        {
            pso_desc.RTVFormats[i] = static_cast<DXGI_FORMAT>(pass_desc->RenderTargetFormats[i]);
        }

        // misc
        pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso_desc.SampleMask = UINT_MAX;
        pso_desc.SampleDesc.Count = 1;
        pso_desc.SampleDesc.Quality = 0;

        ID3D12PipelineState* pso;
        ThrowIfFailed(GD3D12RawDevice->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pso)));

        return std::make_shared<PipelineStateObject>(pso);
    }

    std::shared_ptr<PipelineStateObject> D3D12Device::CreateComputePipelineStateObject(const D3D12ShaderProgram* program)
    {
        IDxcBlob* cs = program->mCS->GetShaderByteCode();  

        D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc =
        {
            .pRootSignature = mRootSignature.Get(),
            .CS = CD3DX12_SHADER_BYTECODE(cs->GetBufferPointer(), cs->GetBufferSize()),
        };

        ID3D12PipelineState* pso;
        ThrowIfFailed(GD3D12RawDevice->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&pso)));
        
        return std::make_shared<PipelineStateObject>(pso);
    }

    void D3D12Device::UpdateTextureArraySlice(DeviceTexture2DArray* array, uint32 index, const void* data)
    {
        size_t pixel_size = DirectX::BitsPerPixel(static_cast<DXGI_FORMAT>(array->Format())) / CHAR_BIT;

        // allocate upload buffer
        size_t intermediate_size = GetRequiredIntermediateSize(array->Resource()->Resource(), 0, 1);
        UploadBuffer upload_buffer = mUploadBufferAllocator->Allocate(static_cast<uint32>(intermediate_size));

        D3D12_SUBRESOURCE_DATA resource{
            .pData = data,
            .RowPitch = static_cast<LONG_PTR>(array->TextureWidth() * pixel_size),
            .SlicePitch = static_cast<LONG_PTR>(array->TextureWidth() * array->TextureHeight() * pixel_size),
        };

        // transfer texture data
        ID3D12GraphicsCommandList* command_list = mResourceCommandList[mFrameIndex].Get();
        UpdateSubresources(command_list, array->Resource()->Resource(), upload_buffer.resource, upload_buffer.offset, index, 1, &resource);
    }

    void D3D12Device::UploadToDefaultHeap(ID3D12Resource* resource, const void* data, uint32 size)
    {
        ASSERT(data);

        // allocate upload buffer
        size_t intermediate_size = GetRequiredIntermediateSize(resource, 0, 1);

        ASSERT(size <= intermediate_size);
        UploadBuffer upload_buffer = mUploadBufferAllocator->Allocate(intermediate_size);

        // copy data to the memory in the upload heap
        upload_buffer.Upload(data, size);

        // transfer data from upload buffer to vertex buffer
        ID3D12GraphicsCommandList* command_list = mResourceCommandList[mFrameIndex].Get();

        command_list->CopyBufferRegion(resource, 0, upload_buffer.resource, upload_buffer.offset, size);
    }

    ID3D12RootSignature* D3D12Device::GetRootSignature()
    {
        return mRootSignature.Get();
    }

    DeviceBackBuffer* D3D12Device::GetCurrentBackBuffer()
    {
        return mBackBuffers[mBackBufferIndex].get();
    }

    DeviceVertexBuffer* D3D12Device::GetScreenMeshVertices()
    {
        return mScreenVertexBuffer.get();
    }

    DeviceIndexBuffer* D3D12Device::GetScreenMeshIndicies()
    {
        return mScreenIndexBuffer.get();
    }

    void D3D12Device::ReleaseResource(MemoryAllocation* res)
    {
        ASSERT(res);
        mResourceCache[mFrameIndex].push_back(res);
    }

    MemoryAllocation* D3D12Device::CreateDeviceBuffer(uint32 size, bool unordered_access, const void* initial_data/*=nullptr*/, D3D12_RESOURCE_STATES initial_state/*=D3D12_RESOURCE_STATE_COMMON*/)
    {
        // allocate vertex buffer
        AllocationDesc desc = {
            .resource_desc = CD3DX12_RESOURCE_DESC::Buffer(size, unordered_access ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE),
            .heap_type = D3D12_HEAP_TYPE_DEFAULT,
            .initial_state = D3D12_RESOURCE_STATE_COPY_DEST,
        };

        MemoryAllocation* allocation = mMemoryAllocator->Allocate(desc);

        if (initial_data)
        {
            UploadToDefaultHeap(allocation->Resource(), initial_data, size);

            CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(allocation->Resource(), D3D12_RESOURCE_STATE_COPY_DEST, initial_state);
            mResourceCommandList[mFrameIndex].Get()->ResourceBarrier(1, &barrier);
        }

        return allocation;
    }

    ID3D12RootSignature* D3D12Device::CreateRootSignature() const
    {
        // all SRVs, UAVs, Samplers are in one root descriptor table
        // 3 root descriptor tables: 1(SRV table) + 1(UAV table) + 1(Sampler table)
        // 3 CBV root descriptor (size equal to EConstBufferType_Total)
        std::array<D3D12_ROOT_PARAMETER, EConstantBufferType_Total + 3> root_parameters;

        // CBV root descriotr
        uint32 index = 0;
        for (; index < EConstantBufferType_Total; index++) 
        {
            auto& root_cbv = root_parameters[index];
            root_cbv.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            root_cbv.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            root_cbv.Descriptor = D3D12_ROOT_DESCRIPTOR{ .ShaderRegister = index, .RegisterSpace = 0 };
        }
        
        // SRV table
        // support up to 16 srv, 8 sampler in a single shader for now, 
        CD3DX12_DESCRIPTOR_RANGE srv_range;
        srv_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, ShaderResourceMaxTexture, 0);

        auto& srv_table = root_parameters[index++];
        srv_table.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        srv_table.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        srv_table.DescriptorTable.NumDescriptorRanges = 1;
        srv_table.DescriptorTable.pDescriptorRanges = &srv_range;

        // UAV table
        CD3DX12_DESCRIPTOR_RANGE uav_range;
        uav_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, ShaderResourceMaxUAV, 0);

        auto& uav_table = root_parameters[index++];
        uav_table.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        uav_table.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        uav_table.DescriptorTable.NumDescriptorRanges = 1;
        uav_table.DescriptorTable.pDescriptorRanges = &uav_range;
        
        // sampler table
        CD3DX12_DESCRIPTOR_RANGE sampler_range;
        sampler_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, ShaderResourceMaxSampler, 0);

        auto& sampler_table = root_parameters[index++];
        sampler_table.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        sampler_table.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        sampler_table.DescriptorTable.NumDescriptorRanges = 1;
        sampler_table.DescriptorTable.pDescriptorRanges = &sampler_range;
        
        CD3DX12_ROOT_SIGNATURE_DESC desc;
        desc.Init(static_cast<uint32>(root_parameters.size()), root_parameters.data(), 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> blob;
        ComPtr<ID3DBlob> error_blob;

        HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error_blob);
        if (FAILED(hr)) 
        {
            if (error_blob && error_blob->GetBufferSize())
            {
                const char* errorMessage = static_cast<const char*>(error_blob->GetBufferPointer());
                std::cout << "Error: " << errorMessage << std::endl;
            }
        }
        ThrowIfFailed(hr);

        ID3D12RootSignature* root_signature;
        ThrowIfFailed(mDevice->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root_signature)));
        return root_signature;
    }

    ShaderResourceView D3D12Device::CreateShaderResourceView(const D3D12_SHADER_RESOURCE_VIEW_DESC* desc, D3D12Resource* resource)
    {
        CPUDescriptor srv_descriptor = mCPUDescriptorAllocator->Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        mDevice->CreateShaderResourceView(resource->Resource(), desc, srv_descriptor.CPUDescriptorHandle());
        return ShaderResourceView(resource, std::move(srv_descriptor));
    }

    UnorderAccessView D3D12Device::CreateUnorderedAccessView(const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc, D3D12Resource* resource)
    {
        CPUDescriptor uav_descriptor = mCPUDescriptorAllocator->Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        mDevice->CreateUnorderedAccessView(resource->Resource(), nullptr, desc, uav_descriptor.CPUDescriptorHandle());
        return UnorderAccessView(resource, std::move(uav_descriptor));
    }

    RenderTargetView D3D12Device::CreateRenderTargetView(const D3D12_RENDER_TARGET_VIEW_DESC* desc, D3D12Resource* resource)
    {
        CPUDescriptor cpu_descriptor = mCPUDescriptorAllocator->Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        mDevice->CreateRenderTargetView(resource->Resource(), desc, cpu_descriptor.CPUDescriptorHandle());
        return RenderTargetView(resource, std::move(cpu_descriptor));
    }

    DepthStencilView D3D12Device::CreateDepthStencilView(const D3D12_DEPTH_STENCIL_VIEW_DESC* desc, D3D12Resource* resource)
    {
        CPUDescriptor dsv_descriptor = mCPUDescriptorAllocator->Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        mDevice->CreateDepthStencilView(resource->Resource(), desc, dsv_descriptor.CPUDescriptorHandle());
        return DepthStencilView(resource, std::move(dsv_descriptor));
    }

    ConstantBufferView D3D12Device::CreateConstantBufferView(const D3D12_CONSTANT_BUFFER_VIEW_DESC* desc, D3D12Resource* resource)
    {
        CPUDescriptor descriptor = mCPUDescriptorAllocator->Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        mDevice->CreateConstantBufferView(desc, descriptor.CPUDescriptorHandle());
        return ConstantBufferView(resource, std::move(descriptor));
    }

    inline void D3D12Device::ClearResourceCache(uint32 frame_index)
    {
        for (const auto& res : mResourceCache[frame_index])
        {
            res->Allocator()->Free(res);
        }
        mResourceCache[frame_index].clear();
    }

    std::shared_ptr<DeviceSampler> D3D12Device::CreateSampler(ESamplerFilter filter_mode, ESamplerAddressMode address_mode)
    {
        // sampler
        D3D12_SAMPLER_DESC desc = {};
        desc.Filter = static_cast<D3D12_FILTER>(filter_mode);
        desc.AddressU = static_cast<D3D12_TEXTURE_ADDRESS_MODE>(address_mode);
        desc.AddressV = static_cast<D3D12_TEXTURE_ADDRESS_MODE>(address_mode);
        desc.AddressW = static_cast<D3D12_TEXTURE_ADDRESS_MODE>(address_mode);
        desc.MinLOD = 0;
        desc.MaxLOD = D3D12_FLOAT32_MAX;
        desc.MipLODBias = 0.0f;
        desc.MaxAnisotropy = 16;
        desc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        desc.BorderColor[0] = desc.BorderColor[1] = desc.BorderColor[2] = desc.BorderColor[3] = 1;

        CPUDescriptor descriptor = mCPUDescriptorAllocator->Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        mDevice->CreateSampler(&desc, descriptor.CPUDescriptorHandle());

        return std::make_shared<DeviceSampler>(std::move(descriptor));
    }

    void D3D12Device::BeginFrame()
    {
        mFrameIndex = (mFrameIndex + 1) % FrameResourceCount;

        // clear frame resource
        ThrowIfFailed(mResourceCommandAllocator[mFrameIndex]->Reset());
        ThrowIfFailed(mResourceCommandList[mFrameIndex]->Reset(mResourceCommandAllocator[mFrameIndex].Get(), nullptr));
        mUploadBufferAllocator->NextFrame();

        if (!mResourceInitialized) UNLIKEYLY
        {
            InitializeInternalResource();
        }
    }

    void D3D12Device::EndFrame(D3D12CommandList* render_cmd_list)
    {
        ID3D12GraphicsCommandList* res_cmd_list = mResourceCommandList[mFrameIndex].Get();
        ThrowIfFailed(res_cmd_list->Close());

        if (render_cmd_list) 
        {
            ASSERT(!render_cmd_list->IsOpen());
            ID3D12CommandList* cmd_lists[] = {res_cmd_list, render_cmd_list->GetCommandList()};
            mCommandQueue->ExecuteCommandLists(2, &(cmd_lists[0]));
        }
        else 
        {
            ID3D12CommandList* cmd_lists[] = { res_cmd_list};
            mCommandQueue->ExecuteCommandLists(1, &(cmd_lists[0]));
        }

        WaitForGPUExecution();
    }

    void D3D12Device::WaitForGPUExecution()
    {
        uint32 fence_value = mFenceValue++;
        mCommandQueue->Signal(mFence.Get(), fence_value);

        if (mFence->GetCompletedValue() != fence_value) 
        {
            mFence->SetEventOnCompletion(fence_value, mFenceEvent);
            WaitForSingleObject(mFenceEvent, INFINITE);
        }

        mSwapChain->Present(1, 0);
        mBackBufferIndex = mSwapChain->GetCurrentBackBufferIndex();
    }

    void D3D12Device::LogAdapterOutputs(IDXGIAdapter* adapter)
    {
        UINT i = 0;
        IDXGIOutput* output = nullptr;
        while (adapter->EnumOutputs(i, &output) != DXGI_ERROR_NOT_FOUND)
        {
            DXGI_OUTPUT_DESC desc;
            output->GetDesc(&desc);

            std::wstring text = L"***Output: ";
            text += desc.DeviceName;
            text += L"\n";
            Log(text.c_str());

            LogOutputDisplayModes(output, BackBufferFormat);

            ReleaseCom(output);

            ++i;
        }
    }

    void D3D12Device::LogOutputDisplayModes(IDXGIOutput* output, DXGI_FORMAT format)
    {
        UINT count = 0;
        UINT flags = 0;

        // Call with nullptr to get list count.
        output->GetDisplayModeList(format, flags, &count, nullptr);

        std::vector<DXGI_MODE_DESC> modeList(count);
        output->GetDisplayModeList(format, flags, &count, &modeList[0]);

        for (auto& x : modeList)
        {
            UINT n = x.RefreshRate.Numerator;
            UINT d = x.RefreshRate.Denominator;
            std::wstring text =
                L"Width = " + std::to_wstring(x.Width) + L" " +
                L"Height = " + std::to_wstring(x.Height) + L" " +
                L"Refresh = " + std::to_wstring(n) + L"/" + std::to_wstring(d) +
                L"\n";

            Log(text.c_str());
        }
    }

    void D3D12Device::CheckFeatureSupport()
    {
    }

    // bind root descriptor tables and descriptor heaps
    void D3D12RootParameters::BindGraphics(ID3D12GraphicsCommandList* command_list)
    {
        // bind SRV_CBV_UAV and Sampler heaps
        command_list->SetDescriptorHeaps(mNumHeaps, const_cast<ID3D12DescriptorHeap* const*>(mHeaps.data()));

        // bind SRV_CBV and Sampler descriptor table
        // see CreateRootSignature for more infomation
        if (!mSRVs.Empty())
        {
            mSRVs.FillDescriptor(GD3D12RawDevice, GD3D12Device->GetNullSRV().Descriptor());
            command_list->SetGraphicsRootDescriptorTable(EConstantBufferType_Total, mSRVs.mStart.GPUDescriptorHandle());
        }

        if (!mUAVs.Empty())
        {
            mUAVs.FillDescriptor(GD3D12RawDevice, GD3D12Device->GetNullUAV().Descriptor());
            command_list->SetGraphicsRootDescriptorTable(EConstantBufferType_Total + 1, mUAVs.mStart.GPUDescriptorHandle());
        }

        if (!mSamplers.Empty())
        {
            mSamplers.AssertDescriptorFull(); // the samplers are fixed for now, all slots in the @mSamplers should be binded
            command_list->SetGraphicsRootDescriptorTable(EConstantBufferType_Total + 2, mSamplers.mStart.GPUDescriptorHandle());
        }
    }

    void D3D12RootParameters::BindCompute(ID3D12GraphicsCommandList* command_list)
    {
        // bind SRV_CBV_UAV and Sampler heaps
        command_list->SetDescriptorHeaps(mNumHeaps, const_cast<ID3D12DescriptorHeap* const*>(mHeaps.data()));

        // bind SRV_CBV and Sampler descriptor table
        // see CreateRootSignature for more infomation
        if (!mSRVs.Empty())
        {
            mSRVs.FillDescriptor(GD3D12RawDevice, GD3D12Device->GetNullSRV().Descriptor());
            command_list->SetComputeRootDescriptorTable(EConstantBufferType_Total, mSRVs.mStart.GPUDescriptorHandle());
        }

        if (!mUAVs.Empty())
        {
            mUAVs.FillDescriptor(GD3D12RawDevice, GD3D12Device->GetNullUAV().Descriptor());
            command_list->SetComputeRootDescriptorTable(EConstantBufferType_Total + 1, mUAVs.mStart.GPUDescriptorHandle());
        }

        if (!mSamplers.Empty())
        {
            mSamplers.AssertDescriptorFull();
            command_list->SetComputeRootDescriptorTable(EConstantBufferType_Total + 2, mSamplers.mStart.GPUDescriptorHandle());
        }
    }

}
