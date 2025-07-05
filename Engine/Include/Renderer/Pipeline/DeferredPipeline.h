#pragma once

#include "Renderer/Pipeline/IPipeline.h"
#include "Renderer/Scene.h"
#include "Renderer/Camera.h"

namespace MRenderer 
{
    class PreFilterEnvMapPass : public IRenderPass
    {
    public:
        static constexpr std::string_view Output_PrefilterEnvMap = "PrefilterEnvMap";

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
            mPrefilterEnvMap = GD3D12Device->CreateTextureCube(PreFilterEnvMapSize, PreFilterEnvMapSize, PreFilterEnvMapMipsLevel, ETextureFormat_R16G16B16A16_FLOAT, true);
        }

        void Connect()
        {
            WritePersistent(Output_PrefilterEnvMap, mPrefilterEnvMap.get());
        }

        void Execute(D3D12CommandList* cmd, Scene* scene, Camera* camera) override;

    protected:
        // each shading state is responsible for generating one mip level
        std::array<ShadingState, PreFilterEnvMapMipsLevel> mShadingState;
        std::shared_ptr<DeviceTexture2DArray> mPrefilterEnvMap;
        bool mReady;
    };

    class PrecomputeBRDFPass : public IRenderPass
    {
    public:
        static constexpr std::string_view Output_PrecomputeBRDF = "PrecomputeBRDF";

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
            mPrecomputeBRDF = GD3D12Device->CreateTexture2D(512, 512, 1, ETextureFormat_R16G16_FLOAT, true);

            mShadingState.SetShader("precompute_brdf.hlsl", true);
            mShadingState.SetRWTexture("PrecomputeBRDF", mPrecomputeBRDF.get());
        }

        void Execute(D3D12CommandList* cmd, Scene* scene, Camera* camera) override;

        void Connect()
        {
            WritePersistent("PrecomputeBRDF", mPrecomputeBRDF.get());
        }

    protected:
        ShadingState mShadingState;
        std::shared_ptr<DeviceTexture2D> mPrecomputeBRDF;
        bool mReady;
    };

    class GBufferPass : public IRenderPass 
    {
    public:
        static constexpr std::string_view Output_GBufferA = "GBufferA";
        static constexpr std::string_view Output_GBufferB = "GBufferB";
        static constexpr std::string_view Output_GBufferC = "GBufferC";

    public:
        GBufferPass()
            :IRenderPass(), mCullingStatus{}
        {
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

        void Connect()
        {
            WriteRenderTarget(Output_GBufferA, TextureFormatKey(GD3D12Device->Width(), GD3D12Device->Height(), ETextureFormat_R8G8B8A8_UNORM));
            WriteRenderTarget(Output_GBufferB, TextureFormatKey(GD3D12Device->Width(), GD3D12Device->Height(), ETextureFormat_FORMAT_R10G10B10A2_UNORM));
            WriteRenderTarget(Output_GBufferC, TextureFormatKey(GD3D12Device->Width(), GD3D12Device->Height(), ETextureFormat_R8G8B8A8_UNORM));
            WriteDepthStencil(GD3D12Device->Width(), GD3D12Device->Height());
        }

        void Execute(D3D12CommandList* cmd, Scene* scene, Camera* camera) override;

        void DrawModel(D3D12CommandList* cmd, SceneModel* obj);

    protected:
        ShadingState mShadingState;
        PipelineStateDesc mPipelineStateDesc;
        FrustumCullStatus mCullingStatus;
    };

    class DeferredShadingPass : public IRenderPass 
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
        };

        static constexpr std::string_view Output_DeferredShadingRT = "DeferredShadingRT";

    public:
        DeferredShadingPass() 
            :IRenderPass(), mGBufferA(nullptr), mGBufferB(nullptr), mGBufferC(nullptr), mDepthStencil(nullptr), mPrefilterEnvMap(nullptr), mPrecomputeBRDF(nullptr)
        {
            mShadingState.SetShader("deferred_shading.hlsl", false);

            // stencil ref is 0, the stencil value of undrawn areas is 0. We mask out all undrawn areas by testing [stencil_ref(0) < stencil_value]
            // - drawn rawn areas (stencil_value >= 1) pass the test (0 < stencil is true)
            // - undrawn areas (stencil_value = 0) fail the test (0 < 0 is false)
            mPipelineStateDesc = PipelineStateDesc::DrawScreen();
            mPipelineStateDesc.StencilTestEnable = true;
            mPipelineStateDesc.FrontFaceStencilDesc = mPipelineStateDesc.BackFaceStencilDesc = StencilTestDesc::Compare(ECompareFunction_Less);
        }

        void Connect(GBufferPass* gbuffer_pass, PreFilterEnvMapPass* env_map_pass, PrecomputeBRDFPass* precompute_brdf_pass)
        {
            mGBufferA = SampleTexture(gbuffer_pass, GBufferPass::Output_GBufferA);
            mGBufferB = SampleTexture(gbuffer_pass, GBufferPass::Output_GBufferB);
            mGBufferC = SampleTexture(gbuffer_pass, GBufferPass::Output_GBufferC);
            mDepthStencil = SampleTexture(gbuffer_pass->GetDepthStencil());
            mPrefilterEnvMap = SampleTexture(env_map_pass, PreFilterEnvMapPass::Output_PrefilterEnvMap);
            mPrecomputeBRDF = SampleTexture(precompute_brdf_pass, PrecomputeBRDFPass::Output_PrecomputeBRDF);

            WriteRenderTarget(Output_DeferredShadingRT, TextureFormatKey(GD3D12Device->Width(), GD3D12Device->Height(), ETextureFormat_R16G16B16A16_FLOAT));
            WriteDepthStencil(gbuffer_pass->GetDepthStencil()); // not actually writting to it, just for enable stencil test
        }

    protected:
        void Execute(D3D12CommandList* cmd, Scene* scene, Camera* camera) override;

    protected:
        RenderPassNode* mGBufferA;
        RenderPassNode* mGBufferB;
        RenderPassNode* mGBufferC;
        RenderPassNode* mDepthStencil;
        RenderPassNode* mPrefilterEnvMap;
        RenderPassNode* mPrecomputeBRDF;

        ShadingState mShadingState;
        PipelineStateDesc mPipelineStateDesc;
    };

    class SkyboxPass : public IRenderPass 
    {
    public:
        static constexpr std::string_view Output_LuminanceTexture = DeferredShadingPass::Output_DeferredShadingRT;

        static constexpr EVertexFormat SkyBoxMeshFormat = EVertexFormat_P3F_N3F_T3F_C3F_T2F;

    public:
        SkyboxPass();

        void Connect(GBufferPass* gbuffer_pass, DeferredShadingPass* shading_pass)
        {
            mShadingState.SetShader("skybox.hlsl", false);

            WriteRenderTarget(Output_LuminanceTexture, shading_pass->FindOutput(DeferredShadingPass::Output_DeferredShadingRT));
            WriteDepthStencil(gbuffer_pass->GetDepthStencil());
        }

        void Execute(D3D12CommandList* cmd, Scene* scene, Camera* camera) override;

    protected:
        std::shared_ptr<DeviceIndexBuffer> mBoxIndexBuffer;
        std::shared_ptr<DeviceVertexBuffer> mBoxVertexBuffer;
        
        ShadingState mShadingState;
    };

    class ClusteredPass : public IRenderPass
    {
    public:
        static constexpr int32 ClusterSizeX = 24;
        static constexpr int32 ClusterSizeY = 16;
        static constexpr int32 ClusterSizeZ = 9;
        static constexpr int32 NumLights = 256;

        // make sure the defination of @Cluster is same as compute shader
        struct alignas(4) Cluster
        {
            Vector3 MinBound;
            float Padding;
            Vector3 MaxBound;
            int NumLights;
            int LightIndex[32];
        };

        struct alignas(4) PointLight
        {
            Vector3 Position;
            float Radius;
            Vector3 Color;
            float Intensity;
        };

        struct ShaderConstant
        {
            int ClusterX;
            int ClusterY;
            int ClusterZ;
            int NumLight;
        };

    public:
        ClusteredPass();

        void Execute(D3D12CommandList* cmd, Scene* scene, Camera* camera) override;

    protected:
        ShadingState mClusterCompute;
        ShadingState mClusterCulling;
        std::shared_ptr<DeviceStructuredBuffer> mStructuredBufferCluster;
        std::shared_ptr<DeviceStructuredBuffer> mStructuredBufferPointLight;
        bool mClusterReady; // has the cluster data been computed
    };

    class ToneMappingPass : public IRenderPass
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
        
            static constexpr std::string_view LuminanceHistogram = "LuminanceHistogram";
            static constexpr std::string_view AverageLuminance = "AverageLuminance";
        };

        struct ToneMappingShader 
        {
            static constexpr std::string_view AverageLuminance = "AverageLuminance";
        };


        static constexpr float MinLogLuminance = -10.0f;
        static constexpr float MaxLogLuminance = 2.0f;
        static constexpr float LogLuminanceRange = MaxLogLuminance - MinLogLuminance;
        static constexpr float InvLogLuminanceRange = 1.0f / (MaxLogLuminance - MinLogLuminance);
        static constexpr uint32 HistogramComputeThreadGroupSize = 16;
        static constexpr uint32 HistogramBinSize = 256;

    public:
        static constexpr std::string_view Output_ToneMappingRT = "LuminanceTexture";

    public:
        ToneMappingPass()
            : mLuminanceTexture(nullptr)
        {
            mLuminanceHistogram = GD3D12Device->CreateStructuredBuffer(HistogramBinSize * sizeof(uint32), sizeof(uint32));
            mLuminanceHistogram->Resource()->SetName(L"LuminanceHistogram");

            float initial_avg_luminance = 0;
            mAverageLuminance = GD3D12Device->CreateStructuredBuffer(1 * sizeof(float), sizeof(float), &initial_avg_luminance);
            mAverageLuminance->Resource()->SetName(L"AverageLuminance");
        }

        void Connect(SkyboxPass* skybox_pass);
        void Execute(D3D12CommandList* cmd, Scene* scene, Camera* camera) override;

    protected:
        RenderPassNode* mLuminanceTexture;

        std::shared_ptr<DeviceStructuredBuffer> mLuminanceHistogram;
        std::shared_ptr<DeviceStructuredBuffer> mAverageLuminance;

        ShadingState mLuminanceHistogramCompute;
        ShadingState mAvarageLuminanceCompute;
        ShadingState mToneMappingRender;
    };

    class PresentPass : public IRenderPass
    {
    public:
        static constexpr std::string_view Input_FinalTexture = "InputTexture";

    public:
        PresentPass() 
            :mInputTexture(nullptr)
        {
        }

        virtual void Execute(D3D12CommandList* cmd, Scene* scene, Camera* camera);

        void Connect(RenderPassNode* node)
        {
            mInputTexture = SampleTexture(node);
        }

    protected:
        RenderPassNode* mInputTexture;
    };

    class DeferredRenderPipeline : public IRenderPipeline
    {
    public:
        DeferredRenderPipeline();

        void Setup() override;
        FrustumCullStatus GetStatus() const override { return mGBufferPass->GetCullingStatus(); }

    public:
        std::unique_ptr<GBufferPass> mGBufferPass;
        std::unique_ptr<DeferredShadingPass> mDeferredShadingPass;
        std::unique_ptr<SkyboxPass> mSkyboxPass;
        std::unique_ptr<PreFilterEnvMapPass> mPrefilterEnvMapPass;
        std::unique_ptr<PrecomputeBRDFPass> mPrecomputeBRDFPass;
        std::unique_ptr<ToneMappingPass> mToneMappingPass;
        std::unique_ptr<PresentPass> mPresentPass;
    };
}