#pragma once
#include "Renderer/FrameGraph.h"
#include "Renderer/Pipeline/IPipeline.h"
#include "Renderer/Scene.h"
#include "Renderer/Camera.h"

namespace MRenderer 
{
    struct DeferredPipelineResource 
    {
        // texture
        inline static FGResourceId PrefilterEnvMap = FGResourceIDs::Instance()->NameToID("PrefilterEnvMap");
        inline static FGResourceId PrecomputeBRDF = FGResourceIDs::Instance()->NameToID("PrecomputeBRDF");

        inline static FGResourceId GBufferA = FGResourceIDs::Instance()->NameToID("GBufferA");
        inline static FGResourceId GBufferB = FGResourceIDs::Instance()->NameToID("GBufferB");
        inline static FGResourceId GBufferC = FGResourceIDs::Instance()->NameToID("GBufferC");
        inline static FGResourceId DepthStencil = FGResourceIDs::Instance()->NameToID("GBufferDepthStencil");

        inline static FGResourceId DeferredShadingRT = FGResourceIDs::Instance()->NameToID("DeferredShadingRT");
        inline static FGResourceId BloomMipchain = FGResourceIDs::Instance()->NameToID("BloomMipchain");
        inline static FGResourceId BloomTempTexture = FGResourceIDs::Instance()->NameToID("BloomTempTexture");

        inline static FGResourceId ToneMappedTexture = FGResourceIDs::Instance()->NameToID("ToneMappedTexture");

        // structured buffer
        inline static FGResourceId FrustumCluster = FGResourceIDs::Instance()->NameToID("FrustumCluster");
        inline static FGResourceId PointLights = FGResourceIDs::Instance()->NameToID("ClusteredLights");

        inline static FGResourceId LuminanceHistogram = FGResourceIDs::Instance()->NameToID("LuminanceHistogram");
        inline static FGResourceId AverageLuminance = FGResourceIDs::Instance()->NameToID("AverageLuminance");
    };


    class PreFilterEnvMapPass : public ComputePass
    {
    public:
        static constexpr uint32 PreFilterEnvMapSize = 512;
        static constexpr uint32 PreFilterEnvMapMipsLevel = 5;
        static constexpr uint32 DispatchGroupSize = 8;
        static constexpr uint32 MinimumMipSize = PreFilterEnvMapSize >> (PreFilterEnvMapMipsLevel - 1);

        // we assert that the smallest mip size is the a multiple of the @DispatchGroupSize, so we don't need to do the size check in compute shader
        static_assert((MinimumMipSize % DispatchGroupSize == 0 ) && (MinimumMipSize > DispatchGroupSize));

        struct ConstantBuffer
        {
            float Roughness;
            uint32 MipLevel;
            uint32 EnvMapSize;
        };

    public:
        PreFilterEnvMapPass()
            :mReady(false)
        {
            mPrefilterEnvMap = GD3D12ResourceAllocator->CreateTextureCube(PreFilterEnvMapSize, PreFilterEnvMapSize, PreFilterEnvMapMipsLevel, ETextureFormat_R16G16B16A16_FLOAT, true);

            WritePersistentResource(DeferredPipelineResource::PrefilterEnvMap, mPrefilterEnvMap.get());
        }

        void Execute(FGContext* context) override;

    protected:
        // each shading state is responsible for generating one mip level
        std::array<ShadingState, PreFilterEnvMapMipsLevel> mShadingState;
        std::shared_ptr<DeviceTexture2DArray> mPrefilterEnvMap;

        bool mReady;
    };

    class PrecomputeBRDFPass : public ComputePass
    {
    protected:
        struct ConstantBuffer 
        {
            uint32 TextureResolution;
        };

        static constexpr uint32 TextureResolution = 512;
    public:
        PrecomputeBRDFPass()
            : mReady(false)
        {
            mPrecomputeBRDF = GD3D12ResourceAllocator->CreateTexture2D(512, 512, 1, ETextureFormat_R16G16_FLOAT, ETexture2DFlag_AllowUnorderedAccess);

            WritePersistentResource(DeferredPipelineResource::PrecomputeBRDF, mPrecomputeBRDF.get());

            mShadingState.SetShader("precompute_brdf.hlsl", true);
            mShadingState.SetRWTexture("PrecomputeBRDF", mPrecomputeBRDF.get());
        }

        void Execute(FGContext* context) override;

    protected:
        ShadingState mShadingState;
        std::shared_ptr<DeviceTexture2D> mPrecomputeBRDF;
        bool mReady;
    };

    class GBufferPass : public GraphicsPass 
    {
    public:
        GBufferPass()
            :GraphicsPass(), mCullingStatus{}
        {
            WriteTransientTexture(DeferredPipelineResource::GBufferA, GD3D12Device->Width(), GD3D12Device->Height(), 1, ETextureFormat_R8G8B8A8_UNORM);
            WriteTransientTexture(DeferredPipelineResource::GBufferB, GD3D12Device->Width(), GD3D12Device->Height(), 1, ETextureFormat_R8G8B8A8_UNORM);
            WriteTransientTexture(DeferredPipelineResource::GBufferC, GD3D12Device->Width(), GD3D12Device->Height(), 1, ETextureFormat_R8G8B8A8_UNORM);
            WriteTransientTexture(DeferredPipelineResource::DepthStencil, GD3D12Device->Width(), GD3D12Device->Height(), 1, ETextureFormat_DepthStencil, ETexture2DFlag_AllowDepthStencil);

            mShadingState.SetShader("gbuffer.hlsl", false);

            // mark the stencil buffer where the object is rendered. This is for culling unused pixels when executing the draw screen command in @DeferredShadingPass.
            mPipelineStateDesc = PipelineStateDesc::DefaultOpaque();
            mPipelineStateDesc.StencilTestEnable = true;
            mPipelineStateDesc.StencilWriteEnable = true;
            mPipelineStateDesc.FrontFaceStencilDesc = mPipelineStateDesc.BackFaceStencilDesc = StencilTestDesc
            {
                .StencilCompareFunc = ECompareFunction_Always,
                .StencilDepthPassOP = EStencilOperation_IncreaseSAT,
                .StencilPassDepthFailOP = EStencilOperation_Keep,
                .StencilFailOP = EStencilOperation_Keep,
            };
        }

        inline const FrustumCullStatus& GetCullingStatus() const { return mCullingStatus; }

        void Execute(FGContext* context) override;

        void DrawModel(D3D12CommandList* cmd, SceneModel* obj);

    protected:
        ShadingState mShadingState;
        PipelineStateDesc mPipelineStateDesc;
        FrustumCullStatus mCullingStatus;
    };

    class DeferredShadingPass : public GraphicsPass
    {
    public:
        struct DeferredShadingShader 
        {
            static constexpr std::string_view GBufferA = "GBufferA";
            static constexpr std::string_view GBufferB = "GBufferB";
            static constexpr std::string_view GBufferC = "GBufferC";
            static constexpr std::string_view DepthStencil = "DepthStencil";
            static constexpr std::string_view PrefilterEnvMap = "PrefilterEnvMap";
            static constexpr std::string_view PrecomputeBRDF = "PrecomputeBRDF";
            static constexpr std::string_view Clusters = "Clusters";
            static constexpr std::string_view PointLights = "PointLights";
        };

        static constexpr ETextureFormat DeferredShadingRTFormat = ETextureFormat_R16G16B16A16_FLOAT;

    public:
        DeferredShadingPass() 
            :GraphicsPass()
        {
            ReadResource(DeferredPipelineResource::GBufferA);
            ReadResource(DeferredPipelineResource::GBufferB);
            ReadResource(DeferredPipelineResource::GBufferC);
            ReadResource(DeferredPipelineResource::DepthStencil);
            ReadResource(DeferredPipelineResource::PrefilterEnvMap);
            ReadResource(DeferredPipelineResource::PrecomputeBRDF);
            ReadResource(DeferredPipelineResource::PointLights);
            ReadResource(DeferredPipelineResource::FrustumCluster);

            WriteTransientTexture(DeferredPipelineResource::DeferredShadingRT, GD3D12Device->Width(), GD3D12Device->Height(), 1, ETextureFormat_R16G16B16A16_FLOAT);
            WriteResource(DeferredPipelineResource::DepthStencil); // just for stencil test, no writting here

            mShadingState.SetShader("deferred_shading.hlsl", false);

            // stencil ref is 0, the stencil value of undrawn areas is 0. We mask out all undrawn areas by testing [stencil_ref(0) < stencil_value]
            // - drawn rawn areas (stencil_value >= 1) pass the test (0 < stencil is true)
            // - undrawn areas (stencil_value = 0) fail the test (0 < 0 is false)
            mPipelineStateDesc = PipelineStateDesc::DrawScreen();
            mPipelineStateDesc.StencilTestEnable = true;
            mPipelineStateDesc.FrontFaceStencilDesc = mPipelineStateDesc.BackFaceStencilDesc = StencilTestDesc::Compare(ECompareFunction_Less);
        }

    protected:
        void Execute(FGContext* context) override;

    protected:
        ShadingState mShadingState;
        PipelineStateDesc mPipelineStateDesc;
    };

    class SkyboxPass : public GraphicsPass 
    {
    public:
        static constexpr EVertexFormat SkyBoxMeshFormat = EVertexFormat_P3F_N3F_T3F_C3F_T2F;

    public:
        SkyboxPass();
        void Execute(FGContext* context) override;

    protected:
        std::shared_ptr<DeviceIndexBuffer> mBoxIndexBuffer;
        std::shared_ptr<DeviceVertexBuffer> mBoxVertexBuffer;

        ShadingState mShadingState;
    };

    class BloomPass : public ComputePass
    {
    public:
        static constexpr uint32 BloomStep = 3;
        static constexpr uint32 MipmapLevel = BloomStep + 2; // see BloomPass::Execute comment
        
        struct PrefilterShader 
        {
            struct ConstantBuffer
            {
                Vector2 TexelSize;
                float Threshold;
                float Knee;
            };

            static constexpr std::string_view ShaderFile = "bloom_prefilter.hlsl";
            static constexpr std::string_view InputTexture = "InputTexture";
            static constexpr std::string_view OutputTexture = "OutputTexture";
            static constexpr uint32 ThreadGroupSizeX = 16;
            static constexpr uint32 ThreadGroupSizeY = 16;
        };

        struct BlurShader 
        {
            struct ConstantBuffer
            {
                Vector2 TexelSize;
            };

            static constexpr std::string_view InputTexture = "InputTexture";
            static constexpr std::string_view OutputTexture = "OutputTexture";
        };

        struct BlurHorizontal : public BlurShader
        {
            static constexpr std::string_view ShaderFile = "blur_horizontal.hlsl";
            static constexpr uint32 ThreadGroupSizeX = 256;
            static constexpr uint32 ThreadGroupSizeY = 1;
            static constexpr uint32 ThreadGroupSizeZ = 1;
        };

        struct BlurVerticalShader : public BlurShader
        {
            static constexpr std::string_view ShaderFile = "blur_vertical.hlsl";
            static constexpr uint32 ThreadGroupSizeX = 1;
            static constexpr uint32 ThreadGroupSizeY = 256;
            static constexpr uint32 ThreadGroupSizeZ = 1;
        };

        struct UpsampleAddShader 
        {
            struct ConstantBuffer
            {
                Vector2 TexelSize;
            };

            static constexpr std::string_view ShaderFile = "bloom_upsample_add.hlsl";
            static constexpr uint32 ThreadGroupSizeX = 256;
            static constexpr uint32 ThreadGroupSizeY = 1;
            static constexpr uint32 ThreadGroupSizeZ = 1;

            static constexpr std::string_view UpperLevel = "UpperLevel";
            static constexpr std::string_view LowerLevel = "LowerLevel";
            static constexpr std::string_view OutputTexture = "OutputTexture";
        };

        struct MergeShader 
        {
            static constexpr std::string_view ShaderFile = "bloom_merge.hlsl";
            static constexpr uint32 ThreadGroupSizeX = 16;
            static constexpr uint32 ThreadGroupSizeY = 16;
            static constexpr uint32 ThreadGroupSizeZ = 1;

            static constexpr std::string_view InputTexture = "InputTexture";
            static constexpr std::string_view OutputTexture = "OutputTexture";
        };

    public:
        BloomPass();
        void Execute(FGContext* context) override;

    protected:
        ShadingState mDownsampleH[BloomStep];
        ShadingState mDownsampleV[BloomStep];
        ShadingState mUpsampleH[BloomStep];
        ShadingState mUpsampleV[BloomStep];
        ShadingState mUpsampleBlurH;
        ShadingState mUpsampleBlurV;
        ShadingState mUpsampleMerge;
        ShadingState mPrefilter;
    };

    class ClusteredPass : public ComputePass
    {
    protected:
        struct ClusteredShaderConstant
        {
            int NumLight;
        };

        struct ClusteredComputeShader
        {
            static constexpr std::string_view ShaderFile = "clustered_compute.hlsl";

            using ShaderConstant = ClusteredShaderConstant;
            static constexpr std::string_view Clusters = "Clusters";
        };

        struct ClusteredCullingShader
        {
            static constexpr std::string_view ShaderFile = "clustered_culling.hlsl";

            using ShaderConstant = ClusteredShaderConstant;
            static constexpr std::string_view Clusters = "Clusters";
            static constexpr std::string_view PointLights = "PointLights";
        };

        static constexpr int32 ClusterSizeX = 24;
        static constexpr int32 ClusterSizeY = 16;
        static constexpr int32 ClusterSizeZ = 9;
        static constexpr int32 MaxSceneLights = 1024;
        static constexpr int32 MaxClusterLights = 128;

        // make sure the defination of @Cluster is same as compute shader
        struct alignas(4) Cluster
        {
            Vector3 MinBound;
            float Padding;
            Vector3 MaxBound;
            int NumLights;
            int LightIndex[MaxClusterLights];
        };

        struct alignas(4) PointLight
        {
            Vector3 Position;
            float Radius;
            Vector3 Color;
            float Intensity;
        };

    public:
        ClusteredPass()
        {
            // allocate structured buffer
            uint32 cluster_size = ClusterSizeX * ClusterSizeY * ClusterSizeZ * sizeof(Cluster);
            uint32 point_light_size = MaxSceneLights * sizeof(PointLight);

            WriteTransientBuffer(DeferredPipelineResource::FrustumCluster, cluster_size, sizeof(Cluster));
            WriteTransientBuffer(DeferredPipelineResource::PointLights, point_light_size, sizeof(PointLight));

            // prepare shader
            mClusteredCompute.SetShader(ClusteredComputeShader::ShaderFile, true);
            mClusteredCulling.SetShader(ClusteredCullingShader::ShaderFile, true);
        }

        void Execute(FGContext* context) override;

    protected:
        ShadingState mClusteredCompute;
        ShadingState mClusteredCulling;
    };

    class AutoExposurePass : public ComputePass 
    {
    protected:
        struct LuminanceHistogramShader
        {
        public:
            struct ConstantBuffer
            {
                uint32 TextureWidth;
                uint32 TextureHeight;
                float MinLogLuminance;
                float InvLogLuminanceRange;
            };

            static constexpr std::string_view ShaderFile = "hdr_luminance_histogram.hlsl";
            static constexpr std::string_view LuminanceTexture = "LuminanceTexture";
            static constexpr std::string_view LuminanceHistogram = "LuminanceHistogram";
        };

        struct AverageLuminanceShader
        {
            struct ConstantBuffer
            {
                uint32 PixelCount;
                float MinLogLuminance;
                float LogLuminanceRange;
            };

            static constexpr std::string_view ShaderFile = "hdr_average_histogram.hlsl";
            static constexpr std::string_view LuminanceHistogram = "LuminanceHistogram";
            static constexpr std::string_view AverageLuminance = "AverageLuminance";
        };

        static constexpr float MinLogLuminance = -10.0f;
        static constexpr float MaxLogLuminance = 2.0f;
        static constexpr float LogLuminanceRange = MaxLogLuminance - MinLogLuminance;
        static constexpr float InvLogLuminanceRange = 1.0f / (MaxLogLuminance - MinLogLuminance);
        static constexpr uint32 HistogramComputeThreadGroupSize = 16;
        static constexpr uint32 HistogramBinSize = 256;
    public:
        AutoExposurePass()
            : mAvarageLuminanceInitialized(false)
        {
            ReadResource(DeferredPipelineResource::DeferredShadingRT);
            WriteTransientBuffer(DeferredPipelineResource::LuminanceHistogram, HistogramBinSize * sizeof(uint32), sizeof(uint32));
            WriteTransientBuffer(DeferredPipelineResource::AverageLuminance, 1 * sizeof(float), sizeof(float));

            mLuminanceHistogramCompute.SetShader(LuminanceHistogramShader::ShaderFile, true);
            mAvarageLuminanceCompute.SetShader(AverageLuminanceShader::ShaderFile, true);
        }

        void Execute(FGContext* context) override;

    protected:
        ShadingState mLuminanceHistogramCompute;
        ShadingState mAvarageLuminanceCompute;

        bool mAvarageLuminanceInitialized;
    };

    class ToneMappingPass : public GraphicsPass
    {
    protected:
        struct ToneMappingShader 
        {
            static constexpr std::string_view AverageLuminance = "AverageLuminance";
            static constexpr std::string_view LuminanceTexture = "LuminanceTexture";
        };

    public:
        ToneMappingPass()
            : mAvarageLuminanceInitialized(false)
        {
            ReadResource(DeferredPipelineResource::DeferredShadingRT);
            ReadResource(DeferredPipelineResource::AverageLuminance);
            WriteTransientTexture(DeferredPipelineResource::ToneMappedTexture, GD3D12Device->Width(), GD3D12Device->Height(), 1, ETextureFormat_R8G8B8A8_UNORM);

            mLuminanceHistogramCompute.SetShader("hdr_luminance_histogram.hlsl", true);
            mAvarageLuminanceCompute.SetShader("hdr_average_histogram.hlsl", true);
            mToneMappingRender.SetShader("hdr_tone_mapping.hlsl", false);
        }

        void Execute(FGContext* context) override;

    protected:
        ShadingState mLuminanceHistogramCompute;
        ShadingState mAvarageLuminanceCompute;
        ShadingState mToneMappingRender;

        bool mAvarageLuminanceInitialized;
    };

    class DeferredRenderPipeline : public IRenderPipeline
    {
    public:
        DeferredRenderPipeline();

        std::vector<IRenderPass*> Setup() override;
        FrustumCullStatus GetStatus() const override { return mGBufferPass->GetCullingStatus(); }

    public:
        std::unique_ptr<GBufferPass> mGBufferPass;
        std::unique_ptr<DeferredShadingPass> mDeferredShadingPass;
        std::unique_ptr<SkyboxPass> mSkyboxPass;
        std::unique_ptr<BloomPass> mBloomPass;
        std::unique_ptr<PreFilterEnvMapPass> mPrefilterEnvMapPass;
        std::unique_ptr<PrecomputeBRDFPass> mPrecomputeBRDFPass;
        std::unique_ptr<AutoExposurePass> mAutoExposurePass;
        std::unique_ptr<ToneMappingPass> mToneMappingPass;
        std::unique_ptr<ClusteredPass> mClusteredPass;
    };
}