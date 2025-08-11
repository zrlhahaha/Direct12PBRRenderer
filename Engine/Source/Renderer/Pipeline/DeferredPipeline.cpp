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
        return AlignUp(texture_size, thread_group_size) / thread_group_size;
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
        mBloomPass = std::make_unique<BloomPass>();

        SetPresentPass(mPresentPass.get());
    }

    void DeferredRenderPipeline::Setup()
    {
        mGBufferPass->Connect();
        mPrefilterEnvMapPass->Connect();
        mPrecomputeBRDFPass->Connect();
        mDeferredShadingPass->Connect(mGBufferPass.get(), mPrefilterEnvMapPass.get(), mPrecomputeBRDFPass.get());
        mSkyboxPass->Connect(mGBufferPass.get(), mDeferredShadingPass.get());
        mBloomPass->Connect(mSkyboxPass.get());
        mToneMappingPass->Connect(mBloomPass.get());
        mPresentPass->Connect(mToneMappingPass->FindOutput(ToneMappingPass::Output_ToneMappingRT));
    }

    SkyboxPass::SkyboxPass()
    {
        static MeshData mesh = DefaultResource::StandardSphereMesh();

        mBoxIndexBuffer = GD3D12Device->CreateIndexBuffer(mesh.Indicies().GetData(), mesh.IndiciesCount() * sizeof(IndexType));
        mBoxVertexBuffer = GD3D12Device->CreateVertexBuffer(mesh.Vertices().GetData(), mesh.VerticesCount() * sizeof(Vertex<SkyBoxMeshFormat>), sizeof(Vertex<SkyBoxMeshFormat>));
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

                cmd->Dispatch(&mShadingState[i], thread_group_num, thread_group_num, NumCubeMapFaces);
            }
        }
    }

    void PrecomputeBRDFPass::Execute(D3D12CommandList* cmd, Scene* scene, Camera* camera)
    {
        if (!mReady)
        {
            mReady = true;

            PIXScope(cmd, "Precompute BRDF Pass");

            constexpr uint32 ThreadGroupSize = 8;
            constexpr uint32 ThreadGroupCount = 512 / ThreadGroupSize;

            ConstantBuffer cbuffer =
            {
                .TextureResolution = TextureResolution
            };

            mShadingState.SetConstantBuffer(cbuffer);
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

        mCullingStatus.NumCulled = scene->GetMeshCount() - mCullingStatus.NumDrawCall;
    }

    void GBufferPass::DrawModel(D3D12CommandList* cmd, SceneModel* obj)
    {
        MeshResource* mesh = obj->GetModel()->GetMeshResource();
        
        // issue draw call
        for (uint32 i = 0; i < mesh->GetSubMeshes().size(); i++)
        {
            mCullingStatus.NumDrawCall++;

            // update per object constant buffer
            MaterialResource* material = obj->GetModel()->GetMaterial(i);
            ShadingState* shading_state = material->GetShadingState();

            ConstantBufferInstance cb = {};

            // copy shader parameters from parameter table to constant buffer
            material->ApplyShaderParameter(cb, shading_state->GetShader(), ConstantBufferInstance::SemanticName);
            cb.Model = obj->GetWorldMatrix(),
            cb.InvModel = obj->GetWorldMatrix().Inverse(),

            obj->GetConstantBuffer()->CommitData(cb);
            cmd->SetGrphicsConstant(EConstantBufferType_Instance, obj->GetConstantBuffer()->GetCurrendConstantBufferView());

            // setup PSO
            cmd->SetGraphicsPipelineState(mesh->GetVertexFormat(), &mPipelineStateDesc, GetPassStateDesc(), shading_state->GetShader());
            
            // issue drawcall
            const SubMeshData& sub_mesh = mesh->GetSubMeshes()[i];
            cmd->DrawMesh(shading_state, mesh->GetVertexFormat(), mesh->GetVertexBuffer(), mesh->GetIndexBuffer(), sub_mesh.Index, sub_mesh.IndicesCount);
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
    
    void ToneMappingPass::Connect(BloomPass* bloom_pass)
    {
        mLuminanceTexture = SampleTexture(bloom_pass, BloomPass::Output_LuminanceTeture);
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

            mLuminanceHistogram->Resource()->TransitionBarrier(cmd->GetCommandList(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

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


            static bool once = false;
            if (!once) 
            {
                //once = true;
                cmd->Dispatch(&mLuminanceHistogramCompute, dispatch_size_x, dispatch_size_y, 1);
            }
        }

        {
            PIXScope(cmd, "Average Luminance Pass");

            mLuminanceHistogram->Resource()->TransitionBarrier(cmd->GetCommandList(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            mAverageLuminance->Resource()->TransitionBarrier(cmd->GetCommandList(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            mAvarageLuminanceCompute.SetRWStructuredBuffer(AverageLuminanceShader::LuminanceHistogram, mLuminanceHistogram.get());
            mAvarageLuminanceCompute.SetRWStructuredBuffer(AverageLuminanceShader::AverageLuminance, mAverageLuminance.get());
            mAvarageLuminanceCompute.SetConstantBuffer(AverageLuminanceShader::ConstantBuffer
                {
                    .PixelCount = tex_width * tex_height,
                    .MinLogLuminance = MinLogLuminance,
                    .LogLuminanceRange = LogLuminanceRange,
                }
            );

            cmd->Dispatch(&mAvarageLuminanceCompute, 1, 1, 1);
        }

        {
            PIXScope(cmd, "Tone Mapping Pass");

            mAverageLuminance->Resource()->TransitionBarrier(cmd->GetCommandList(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            mToneMappingRender.SetRWStructuredBuffer(ToneMappingShader::AverageLuminance, mAverageLuminance.get());
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

    BloomPass::BloomPass()
        :mInputTexture(nullptr)
    {
        mPrefilter.SetShader(PrefilterShader::ShaderFile, true);
        mUpsampleBlurH.SetShader(BlurHorizontal::ShaderFile, true);
        mUpsampleBlurV.SetShader(BlurVerticalShader::ShaderFile, true);
        mUpsampleMerge.SetShader(MergeShader::ShaderFile, true);

        for (auto& shading_state : mDownsampleH)
        {
            shading_state.SetShader(BlurHorizontal::ShaderFile, true);
        }

        for (auto& shading_state : mDownsampleV)
        {
            shading_state.SetShader(BlurVerticalShader::ShaderFile, true);
        }

        for (auto& shading_state : mUpsampleH)
        {
            shading_state.SetShader(UpsampleAddShader::ShaderFile, true);
        }

        for (auto& shading_state : mUpsampleV)
        {
            shading_state.SetShader(BlurVerticalShader::ShaderFile, true);
        }
    }

    // ref: https://zhuanlan.zhihu.com/p/525500877
    //      https://catlikecoding.com/unity/tutorials/custom-srp/hdr/
    //      introductionto3dgameprogrammingwithdirectx12 section 13.7
    // whole process is like:
    // downsample part
    // A[1] = Prefilter(S)
    // B[2] = DownsampleH(A[1])
    // A[2] = DownsampleV(B[2])
    // B[3] = DownsampleH(A[2])
    // A[3] = DownsampleV(B[3])

    // upsample part
    // B[2] = UpsampleH(A[2]) + UpsampleH(A[3])
    // A[2] = UpsampleV(B[2])
    // B[1] = UpsampleH(A[1]) + UpsampleH(A[2])
    // A[1] = UpsampleV(B[1])

    // merge part
    // B[0] = UpsampleH(A[1])
    // A[0] = UpsampleV(B[0]) + S

    // A and B are intermediate textures, S is the original texture. they all are the same size except A and B have mipmap
    // Prefilter is for extract highlight area and fighting fireflies. DownsampleX and UpsampleX are guassian filters that are split into horizontal and vertical parts.
    // 1 step meas a horizontal and a vertical process, example above has 2 steps, and the mipmap level equal steps + 2 which is 4.
    void BloomPass::Execute(D3D12CommandList* cmd, Scene* scene, Camera* camera)
    {
        PIXScope(cmd, "Bloom Pass");

        DeviceTexture2D* original_tex = dynamic_cast<DeviceTexture2D*>(mInputTexture->GetResource());
        ASSERT(original_tex);

        // bloom prefilter
        {
            PIXScope(cmd, "Bloom Prefilter");

            mPrefilter.SetConstantBuffer
            (
                PrefilterShader::ConstantBuffer
                {
                    .TexelSize = Vector2(1.0f / (original_tex->Width() >> 1), 1.0f / (original_tex->Height() >> 1)),
                    .Threshold = 1.0f,
                    .Knee = 0.5f,
                }
            );

            mPrefilter.SetTexture(PrefilterShader::InputTexture, original_tex);
            mPrefilter.SetRWTexture(PrefilterShader::OutputTexture, mMipchain.get(), 1);
            cmd->Dispatch(&mPrefilter, CalculateDispatchSize(mMipchain->Width(), PrefilterShader::ThreadGroupSizeX), CalculateDispatchSize(mMipchain->Height(), PrefilterShader::ThreadGroupSizeY), 1);
        }

        {
            PIXScope(cmd, "Bloom Downsample");
            // bloom downsample
            for (int i = 0; i < BloomStep; i++)
            {
                uint32 upper_mip_level = i + 1;
                int32 lower_mip_width = mTempTexture->Width() >> (upper_mip_level + 1);
                int32 lower_mip_height = mTempTexture->Height() >> (upper_mip_level + 1);

                {
                    PIXScope(cmd, "Blur Horizontal");

                    // use gaussian filter for downsampling
                    mDownsampleH[i].SetConstantBuffer
                    (
                        BlurHorizontal::ConstantBuffer
                        {
                            .TexelSize = Vector2(1.0f / lower_mip_width, 1.0f / lower_mip_height),
                        }
                    );

                    // horizontal blur
                    mDownsampleH[i].SetTexture(BlurHorizontal::InputTexture, mMipchain.get(), upper_mip_level);
                    mDownsampleH[i].SetRWTexture(BlurHorizontal::OutputTexture, mTempTexture.get(), upper_mip_level + 1);
                    cmd->Dispatch(&mDownsampleH[i], CalculateDispatchSize(lower_mip_width, BlurHorizontal::ThreadGroupSizeX), CalculateDispatchSize(lower_mip_height, BlurHorizontal::ThreadGroupSizeY), 1);
                }

                {
                    PIXScope(cmd, "Blur Vertical");
                    mDownsampleV[i].SetConstantBuffer(BlurVerticalShader::ConstantBuffer
                        {
                            .TexelSize = Vector2(1.0f / lower_mip_width, 1.0f / lower_mip_height),
                        }
                    );

                    // vertical blur
                    mDownsampleV[i].SetTexture(BlurVerticalShader::InputTexture, mTempTexture.get(), i + 2);
                    mDownsampleV[i].SetRWTexture(BlurVerticalShader::OutputTexture, mMipchain.get(), i + 2);
                    cmd->Dispatch(&mDownsampleV[i], CalculateDispatchSize(lower_mip_width, BlurVerticalShader::ThreadGroupSizeX), CalculateDispatchSize(lower_mip_height, BlurVerticalShader::ThreadGroupSizeY), 1);
                }
            }
        }

        {
            PIXScope(cmd, "Bloom Upsample");

            // guassain filter upper level and lower level and add them together
            // let 2d gaussain filter to be S, and 1d horizontal and vertical gaussian filter to be H and V
            // upslample process is like S(t1) + S(t2) = V(H(t1)) + V(H(t2)) = V(H(t1) + H(t2))
            for (int i = BloomStep - 1; i >= 0; i--)
            {
                int32 upper_mip_level = i + 1;
                int32 upper_mip_width = mTempTexture->Width() >> upper_mip_level;
                int32 upper_mip_height = mTempTexture->Height() >> upper_mip_level;

                {
                    PIXScope(cmd, "Upsample Horizontal Add");

                    // H(t1) + H(t2)
                    mUpsampleH[i].SetConstantBuffer
                    (
                        UpsampleAddShader::ConstantBuffer
                        {
                            .TexelSize = Vector2(1.0f / upper_mip_width, 1.0f / upper_mip_height),
                        }
                    );

                    mUpsampleH[i].SetTexture(UpsampleAddShader::UpperLevel, mMipchain.get(), upper_mip_level);
                    mUpsampleH[i].SetTexture(UpsampleAddShader::LowerLevel, mMipchain.get(), upper_mip_level + 1);
                    mUpsampleH[i].SetRWTexture(UpsampleAddShader::OutputTexture, mTempTexture.get(), i + 1);
                    cmd->Dispatch(&mUpsampleH[i], CalculateDispatchSize(upper_mip_width, UpsampleAddShader::ThreadGroupSizeX), CalculateDispatchSize(upper_mip_height, UpsampleAddShader::ThreadGroupSizeY), 1);
                }

                {
                    PIXScope(cmd, "Blur Vertical");
                    mUpsampleV[i].SetConstantBuffer
                    (
                        BlurVerticalShader::ConstantBuffer
                        {
                            .TexelSize = Vector2(1.0f / upper_mip_width, 1.0f / upper_mip_height),
                        }
                    );

                    // V(t)
                    mUpsampleV[i].SetTexture(BlurVerticalShader::InputTexture, mTempTexture.get(), upper_mip_level);
                    mUpsampleV[i].SetRWTexture(BlurVerticalShader::OutputTexture, mMipchain.get(), upper_mip_level);
                    cmd->Dispatch(&mUpsampleV[i], CalculateDispatchSize(upper_mip_width, BlurVerticalShader::ThreadGroupSizeX), CalculateDispatchSize(upper_mip_height, BlurVerticalShader::ThreadGroupSizeY), 1);
                }
            }
        }

        {
            PIXScope(cmd, "Upsample Merge");
            // merge the blurred texture with the original texture
            // simply gaussain filter upsample and add the original texture

            uint32 upper_mip_width = mMipchain->Width();
            uint32 upper_mip_height = mMipchain->Height();

            {
                PIXScope(cmd, "Blur Horizontal");

                mUpsampleBlurH.SetConstantBuffer
                (
                    BlurHorizontal::ConstantBuffer
                    {
                        .TexelSize = Vector2(1.0f / upper_mip_width, 1.0f / upper_mip_height),
                    }
                );

                mUpsampleBlurH.SetTexture(BlurHorizontal::InputTexture, mMipchain.get(), 1);
                mUpsampleBlurH.SetRWTexture(BlurHorizontal::OutputTexture, mTempTexture.get(), 0);
                cmd->Dispatch(&mUpsampleBlurH, CalculateDispatchSize(upper_mip_width, BlurHorizontal::ThreadGroupSizeX), CalculateDispatchSize(upper_mip_height, BlurHorizontal::ThreadGroupSizeY), 1);
            }

            {
                PIXScope(cmd, "Blur Vertical");
                mUpsampleBlurV.SetConstantBuffer
                (
                    BlurVerticalShader::ConstantBuffer
                    {
                        .TexelSize = Vector2(1.0f / upper_mip_width, 1.0f / upper_mip_height),
                    }
                );

                // vertical blur
                mUpsampleBlurV.SetTexture(BlurVerticalShader::InputTexture, mTempTexture.get(), 0);
                mUpsampleBlurV.SetRWTexture(BlurVerticalShader::OutputTexture, mMipchain.get(), 0);
                cmd->Dispatch(&mUpsampleBlurV, CalculateDispatchSize(upper_mip_width, BlurVerticalShader::ThreadGroupSizeX), CalculateDispatchSize(upper_mip_height, BlurVerticalShader::ThreadGroupSizeY), 1);
            }

            {
                PIXScope(cmd, "Merge");

                // merge with original texture
                mUpsampleMerge.SetTexture(MergeShader::InputTexture , original_tex, 0);
                mUpsampleMerge.SetRWTexture(MergeShader::OutputTexture, mMipchain.get(), 0);
                cmd->Dispatch(&mUpsampleMerge, CalculateDispatchSize(original_tex->Width(), MergeShader::ThreadGroupSizeX), CalculateDispatchSize(original_tex->Height(), MergeShader::ThreadGroupSizeY), 1);
            }
        }
    }
}