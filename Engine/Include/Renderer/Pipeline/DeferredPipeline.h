#pragma once

#include "Renderer/Pipeline/IPipeline.h"
#include "Renderer/Scene.h"
#include "Renderer/Camera.h"

namespace MRenderer 
{
    class PreFilterEnvMapPass : public IRenderPass
    {
    public:
        // smallest mip size is 8x8, this must be the same as the value that compute shader numthreads declared, which is 8x8x6
        // each threadgroup will process 8x8x6 pixels in an array slice of the cube map
        static const uint32 PreFilterEnvMapSize = 512;
        static const uint32 PreFilterEnvMapMipsLevel = 6;
        
        // i.e 8, same as the smallest mip size
        static const uint32 DispatchGroupSize = 512 >> PreFilterEnvMapMipsLevel;

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
            mPrefilterEnvMap = GD3D12Device->CreateTextureCube(PreFilterEnvMapSize, PreFilterEnvMapSize, PreFilterEnvMapMipsLevel, ETextureFormat_R8G8B8A8_UNORM, nullptr, true);
        }

        void Connect()
        {
            WritePersistent("PrefilterEnvMap", mPrefilterEnvMap.get());
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
        struct ConstantBuffer 
        {
            uint32 TextureResolution;
        };

        static constexpr uint32 TextureResolution = 512;
    public:
        PrecomputeBRDFPass()
            : mReady(false)
        {
            mPrecomputeBRDF = GD3D12Device->CreateTexture2D(512, 512, ETextureFormat_R8G8_UNORM, nullptr, true);

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
            WriteRenderTarget("GBufferA", RenderTargetKey(GD3D12Device->Width(), GD3D12Device->Height(), ETextureFormat_R8G8B8A8_UNORM));
            WriteRenderTarget("GBufferB", RenderTargetKey(GD3D12Device->Width(), GD3D12Device->Height(), ETextureFormat_FORMAT_R10G10B10A2_UNORM));
            WriteRenderTarget("GBufferC", RenderTargetKey(GD3D12Device->Width(), GD3D12Device->Height(), ETextureFormat_R8G8B8A8_UNORM));
            WriteDepthStencil(GD3D12Device->Width(), GD3D12Device->Height());
        }

        void Execute(D3D12CommandList* cmd, Scene* scene, Camera* camera) override
        {
            FrustumVolume volume = FrustumVolume::FromMatrix(camera->GetProjectionMatrix() * camera->GetLocalSpaceMatrix());

            mCullingStatus = {};
            scene->CullModel(volume,
                [&](SceneModel* model) 
                {
                    DrawModel(cmd, model); 
                }
            );

            mCullingStatus.NumCulled = scene->GetModelCount() - mCullingStatus.NumDrawCall;
        }

        void DrawModel(D3D12CommandList* cmd, SceneModel* obj);

    protected:
        ShadingState mShadingState;
        PipelineStateDesc mPipelineStateDesc;
        FrustumCullStatus mCullingStatus;
    };

    class DeferredShadingPass : public IRenderPass 
    {
    public:
        DeferredShadingPass() 
            :IRenderPass()
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
            SampleTexture("GBufferA", gbuffer_pass->FindOutput("GBufferA"));
            SampleTexture("GBufferB", gbuffer_pass->FindOutput("GBufferB"));
            SampleTexture("GBufferC", gbuffer_pass->FindOutput("GBufferC"));
            SampleTexture("DepthStencil", gbuffer_pass->GetDepthStencil());
            SampleTexture("PrefilterEnvMap", env_map_pass->FindOutput("PrefilterEnvMap"));
            SampleTexture("PrecomputeBRDF", precompute_brdf_pass->FindOutput("PrecomputeBRDF"));

            WriteRenderTarget("DeferredShadingRT", RenderTargetKey(GD3D12Device->Width(), GD3D12Device->Height(), ETextureFormat_R8G8B8A8_UNORM));
            WriteDepthStencil(gbuffer_pass->GetDepthStencil()); // not actually writting to it, just for enable stencil test
        }

    protected:
        void PostCompile() override
        {
            BindRenderPassInput(&mShadingState);
        }

        void Execute(D3D12CommandList* cmd, Scene* scene, Camera* camera) override;

    protected:
        ShadingState mShadingState;
        PipelineStateDesc mPipelineStateDesc;
    };

    class SkyboxPass : public IRenderPass 
    {
    public:
        static constexpr EVertexFormat SkyBoxMeshFormat = EVertexFormat_P3F_N3F_T3F_C3F_T2F;

    public:
        SkyboxPass();

        void Connect(GBufferPass* gbuffer_pass, DeferredShadingPass* shading_pass)
        {
            mShadingState.SetShader("skybox.hlsl", false);

            WriteRenderTarget("SkyBoxRT", shading_pass->IndexRenderTarget(0));
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
    };
}