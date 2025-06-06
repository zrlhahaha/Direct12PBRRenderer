#include "Renderer/Pipeline/DeferredPipeline.h"
#include "Resource/DefaultResource.h"
#include "Resource/ResourceLoader.h"
#include "Renderer/Scene.h"
#include "Renderer/Device/Direct12/D3D12CommandList.h"
#include "pix3.h"

#define PIXScope(cmd, name) PIXScopedEvent((cmd)->GetCommandList(), PIX_COLOR_DEFAULT, name);

namespace MRenderer 
{
    inline uint32 CalculateDispatchSize(uint32 texture_size, uint32 thread_group_size)
    {
        return static_cast<uint32>(std::ceil(static_cast<float>(texture_size) / static_cast<float>(thread_group_size)));
    }

    DeferredRenderPipeline::DeferredRenderPipeline()
        :IRenderPipeline()
    {
        mGBufferPass = std::make_unique<GBufferPass>();
        mDeferredShadingPass = std::make_unique<DeferredShadingPass>();
        mSkyboxPass = std::make_unique<SkyboxPass>();
        mPrefilterEnvMapPass = std::make_unique<PreFilterEnvMapPass>();
        mPrecomputeBRDFPass = std::make_unique<PrecomputeBRDFPass>();
        mToneMappingPass = std::make_unique<ToneMappingPass>();
        mPresentPass = std::make_unique<PresentPass>();

        SetPresentPass(mPresentPass.get());
    }

    void DeferredRenderPipeline::Setup()
    {
        mGBufferPass->Connect();
        mPrefilterEnvMapPass->Connect();
        mPrecomputeBRDFPass->Connect();
        mDeferredShadingPass->Connect(mGBufferPass.get(), mPrefilterEnvMapPass.get(), mPrecomputeBRDFPass.get());
        mSkyboxPass->Connect(mGBufferPass.get(), mDeferredShadingPass.get());
        mToneMappingPass->Connect(mSkyboxPass.get());
        mPresentPass->Connect(mToneMappingPass->FindOutput(ToneMappingPass::Output_ToneMappingRT));
    }

    SkyboxPass::SkyboxPass()
    {
        static MeshData mesh = DefaultResource::StandardSphereMesh();

        mBoxIndexBuffer = GD3D12Device->CreateIndexBuffer(mesh.IndiciesData(), static_cast<uint32>(mesh.IndiciesCount() * sizeof(IndexType)));
        mBoxVertexBuffer = GD3D12Device->CreateVertexBuffer(mesh.VerticesData(), static_cast<uint32>(mesh.VerticesCount() * sizeof(Vertex<SkyBoxMeshFormat>)), sizeof(Vertex<SkyBoxMeshFormat>));
    }

    void SkyboxPass::Execute(D3D12CommandList* cmd, Scene* scene, Camera* camera)
    {
        auto sky_box = scene->GetSkyBox();

        if (sky_box) 
        {
            PIXScope(cmd, "Skybox Pass");
            mShadingState.SetTexture("SkyBox", sky_box->Resource());

            // pso
            static PipelineStateDesc state = PipelineStateDesc::Generate(true, false, ECullMode_None);
            cmd->SetGraphicsPipelineState(SkyBoxMeshFormat, &state, GetPassStateDesc(), mShadingState.GetShader());

            // draw skybox
            cmd->DrawMesh(&mShadingState, SkyBoxMeshFormat, mBoxVertexBuffer.get(), mBoxIndexBuffer.get(), 0, mBoxIndexBuffer->IndiciesCount());
        }
    }

    void PreFilterEnvMapPass::Execute(D3D12CommandList* cmd, Scene* scene, Camera* camera)
    {

        // generate prefilter cubemap of skybox
        // this pass only need to be executed one time
        if (!mReady) 
        {
            mReady = true;
            
            PIXScope(cmd, "Precompute PrefilterEnvMap Pass");

            for (uint32 i = 0; i < PreFilterEnvMapMipsLevel; i++)
            {
                ShadingState& shading_state = mShadingState[i];
                shading_state.SetShader("env_map_gen.hlsl", true);
                shading_state.SetRWTextureArray("PrefilterEnvMap", mPrefilterEnvMap.get());
                if (scene->GetSkyBox()) 
                {
                    shading_state.SetTexture("SkyBox", scene->GetSkyBox()->Resource());
                }

                ConstantBuffer cbuffer =
                {
                    .Roughness = static_cast<float>(i) / static_cast<float>(PreFilterEnvMapMipsLevel - 1),
                    .MipLevel = i,
                    .EnvMapSize = PreFilterEnvMapSize
                };

                mShadingState[i].SetConstantBuffer(cbuffer);
            }

            for (uint32 i = 0; i < PreFilterEnvMapMipsLevel; i++)
            {
                uint32 mip_size = PreFilterEnvMapSize >> i;
                uint32 thread_group_num = (mip_size + DispatchGroupSize - 1) / DispatchGroupSize;

                cmd->Dispatch(&mShadingState[i], thread_group_num, thread_group_num, 6);
            }
        }
    }

    void PrecomputeBRDFPass::Execute(D3D12CommandList* cmd, Scene* scene, Camera* camera)
    {
        PIXScope(cmd, "Precompute BRDF Pass");

        constexpr uint32 ThreadGroupSize = 8;
        constexpr uint32 ThreadGroupCount = 512 / ThreadGroupSize;

        ConstantBuffer cbuffer =
        {
            .TextureResolution = TextureResolution
        };

        mShadingState.SetConstantBuffer(cbuffer);

        if (!mReady)
        {
            mReady = true;
            cmd->Dispatch(&mShadingState, ThreadGroupCount, ThreadGroupCount, 1);
        }
    }

    void GBufferPass::Execute(D3D12CommandList* cmd, Scene* scene, Camera* camera)
    {
        PIXScope(cmd, "Gbuffer Pass");

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

    void GBufferPass::DrawModel(D3D12CommandList* cmd, SceneModel* obj)
    {
        // update per object constant buffer
        obj->CommitConstantBuffer();
        cmd->SetGrphicsConstant(EConstantBufferType_Instance, obj->GetConstantBuffer()->GetCurrendConstantBufferView());

        MeshResource* mesh = obj->GetModel()->GetMeshResource();
        const MeshData& mesh_data = mesh->GetMeshData();

        // issue draw call
        for (uint32 i = 0; i < mesh_data.GetSubMeshCount(); i++)
        {
            mCullingStatus.NumDrawCall++;

            // setup PSO
            MaterialResource* material = obj->GetModel()->GetMaterial(i);
            ShadingState* shading_state = material->GetShadingState();
            cmd->SetGraphicsPipelineState(mesh_data.GetFormat(), &mPipelineStateDesc, GetPassStateDesc(), shading_state->GetShader());
            
            const SubMeshData& sub_mesh = mesh_data.GetSubMesh(i);
            cmd->DrawMesh(shading_state, mesh_data.GetFormat(), mesh->GetVertexBuffer(), mesh->GetIndexBuffer(), sub_mesh.Index, sub_mesh.IndicesCount);
        }
    }

    void DeferredShadingPass::Execute(D3D12CommandList* cmd, Scene* scene, Camera* camera)
    {
        PIXScope(cmd, "Deferred Shading");

        // bind pass input to shader
        mShadingState.SetTexture(DeferredShadingShader::GBufferA, dynamic_cast<DeviceTexture*>(mGBufferA->GetResource()));
        mShadingState.SetTexture(DeferredShadingShader::GBufferB, dynamic_cast<DeviceTexture*>(mGBufferB->GetResource()));
        mShadingState.SetTexture(DeferredShadingShader::GBufferC, dynamic_cast<DeviceTexture*>(mGBufferC->GetResource()));
        mShadingState.SetTexture(DeferredShadingShader::PrefilterEnvMap, dynamic_cast<DeviceTexture*>(mPrefilterEnvMap->GetResource()));
        mShadingState.SetTexture(DeferredShadingShader::PrecomputeBRDF, dynamic_cast<DeviceTexture*>(mPrecomputeBRDF->GetResource()));

        // setup pso
        cmd->SetStencilRef(0);
        cmd->SetGraphicsPipelineState(EVertexFormat_P3F_T2F, &mPipelineStateDesc, GetPassStateDesc(), mShadingState.GetShader());
        cmd->DrawScreen(&mShadingState);
    }

    ClusteredPass::ClusteredPass()
        :mClusterReady(false)
    {
        // prepare shader
        mClusterCompute.SetShader("clustered_compute.hlsl", true);
        mClusterCulling.SetShader("clustered_culling.hlsl", true);

        // allocate structured buffer
        uint32 cluster_size = ClusterSizeX * ClusterSizeY * ClusterSizeZ * sizeof(Cluster);
        mStructuredBufferCluster = GD3D12Device->CreateStructuredBuffer(cluster_size, sizeof(Cluster));

        uint32 point_light_size = NumLights * sizeof(PointLight);
        mStructuredBufferPointLight = GD3D12Device->CreateStructuredBuffer(point_light_size, sizeof(Cluster));

        // bind structured buffer to shader
        mClusterCompute.SetRWStructuredBuffer("Cluster", mStructuredBufferCluster.get());
        mClusterCompute.SetStructuredBuffer("PointLights", mStructuredBufferPointLight.get());
        mClusterCulling.SetRWStructuredBuffer("Cluster", mStructuredBufferCluster.get());
        mClusterCulling.SetStructuredBuffer("PointLights", mStructuredBufferPointLight.get());
    }

    void ClusteredPass::Execute(D3D12CommandList* cmd, Scene* scene, Camera* camera)
    {
        // set shader constant buffer
        ShaderConstant cbuffer
        {
            .ClusterX = ClusterSizeX,
            .ClusterY = ClusterSizeY,
            .ClusterZ = ClusterSizeZ,
            .NumLight = static_cast<int>(scene->GetLightCount())
        };
        mClusterCompute.SetConstantBuffer(cbuffer);

        // calculate AABB of each cluster, we only need to do this once
        if (!mClusterReady) 
        {
            cmd->Dispatch(&mClusterCompute, ClusterSizeX, ClusterSizeY, ClusterSizeZ);
        }

        // commit lights data
        std::array<PointLight, NumLights> lights;
        ASSERT(scene->GetLightCount() <= NumLights);

        FrustumVolume volume = FrustumVolume::FromMatrix(camera->GetProjectionMatrix() * camera->GetLocalSpaceMatrix());

        int i = 0;
        scene->CullLight(volume,
            [&](SceneLight* light)
            {
                lights[i++] = PointLight
                {
                    .Position = light->GetTranslation(),
                    .Radius = light->GetRadius(),
                    .Color = light->GetColor(),
                    .Intensity = light->GetIntensity()
                };
            }
        );

        mStructuredBufferPointLight->Commit(&lights, sizeof(lights));

        // calculate the lights that intersect with each cluster
        cmd->Dispatch(&mClusterCulling, ClusterSizeX, ClusterSizeY, ClusterSizeZ);
    }
    
    void ToneMappingPass::Connect(SkyboxPass* skybox_pass)
    {
        mLuminanceTexture = SampleTexture(skybox_pass, DeferredShadingPass::Output_DeferredShadingRT);
        WriteRenderTarget(ToneMappingPass::Output_ToneMappingRT, TextureFormatKey(GD3D12Device->Width(), GD3D12Device->Height(), ETextureFormat_R8G8B8A8_UNORM));

        mLuminanceHistogramCompute.SetShader("hdr_luminance_histogram.hlsl", true);
        mAvarageLuminanceCompute.SetShader("hdr_average_histogram.hlsl", true);
        mToneMappingRender.SetShader("hdr_tone_mapping.hlsl", false);
    }

    void ToneMappingPass::Execute(D3D12CommandList* cmd, Scene* scene, Camera* camera)
    {
        PIXScope(cmd, "Auto Exposure Pass");
        uint32 tex_width = mLuminanceTexture->GetTextureFormatKey().Info.Width;
        uint32 tex_height = mLuminanceTexture->GetTextureFormatKey().Info.Height;

        {
            PIXScope(cmd, "Luminance Histogram Pass");


            mLuminanceHistogramCompute.SetRWStructuredBuffer("LuminanceHistogram", mLuminanceHistogram.get());
            mLuminanceHistogramCompute.SetTexture(LuminanceHistogramShader::LuminanceTexture, dynamic_cast<DeviceTexture*>(mLuminanceTexture->GetResource()));
            mLuminanceHistogramCompute.SetConstantBuffer(LuminanceHistogramShader::ConstantBuffer
                {
                    .TextureWidth = tex_width,
                    .TextureHeight = tex_height,
                    .MinLogLuminance = MinLogLuminance,
                    .InvLogLuminanceRange = InvLogLuminanceRange,
                }
            );

            uint32 dispatch_size_x = CalculateDispatchSize(tex_width, HistogramComputeThreadGroupSize);
            uint32 dispatch_size_y = CalculateDispatchSize(tex_height, HistogramComputeThreadGroupSize);

            cmd->Dispatch(&mLuminanceHistogramCompute, dispatch_size_x, dispatch_size_y, 1);
        }

        {
            PIXScope(cmd, "Average Luminance Pass");

            mAvarageLuminanceCompute.SetRWStructuredBuffer(AverageLuminanceShader::LuminanceHistogram, mLuminanceHistogram.get());
            mAvarageLuminanceCompute.SetRWStructuredBuffer(AverageLuminanceShader::AverageLuminance, mAverageLuminance.get());
            mAvarageLuminanceCompute.SetConstantBuffer(AverageLuminanceShader::ConstantBuffer
                {
                    .PixelCount = tex_width * tex_height,
                    .MinLogLuminance = MinLogLuminance,
                    .LogLuminanceRange = LogLuminanceRange,
                }
            );

            cmd->Dispatch(&mAvarageLuminanceCompute, HistogramBinSize, 1, 1);
        }

        {
            PIXScope(cmd, "Tone Mapping Pass");

            mToneMappingRender.SetStructuredBuffer(ToneMappingShader::AverageLuminance, mAverageLuminance.get());
            mToneMappingRender.SetTexture(LuminanceHistogramShader::LuminanceTexture, dynamic_cast<DeviceTexture*>(mLuminanceTexture->GetResource()));

            PipelineStateDesc pso_desc = PipelineStateDesc::DrawScreen();
            cmd->SetGraphicsPipelineState(EVertexFormat_P3F_T2F, &pso_desc, GetPassStateDesc(), mToneMappingRender.GetShader());
            cmd->DrawScreen(&mToneMappingRender);
        }
    }

    void PresentPass::Execute(D3D12CommandList* cmd, Scene* scene, Camera* camera)
    {
        ASSERT(mInputTexture);

        DeviceTexture2D* rt = dynamic_cast<DeviceTexture2D*>(mInputTexture->GetResource());
        ASSERT(rt);

        cmd->Present(rt);
    }
}