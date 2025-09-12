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
    }

    std::vector<IRenderPass*> DeferredRenderPipeline::Setup()
    {
        mPrefilterEnvMapPass = std::make_unique<PreFilterEnvMapPass>();
        mPrecomputeBRDFPass = std::make_unique<PrecomputeBRDFPass>();
        mGBufferPass = std::make_unique<GBufferPass>();
        mDeferredShadingPass = std::make_unique<DeferredShadingPass>();
        mSkyboxPass = std::make_unique<SkyboxPass>();
        mAutoExposurePass = std::make_unique<AutoExposurePass>();
        mToneMappingPass = std::make_unique<ToneMappingPass>();
        mPresentPass = std::make_unique<PresentPass>();
        mBloomPass = std::make_unique<BloomPass>();
        mClusteredPass = std::make_unique<ClusteredPass>();

        mPresentPass->SetFinalTexture(DeferredPipelineResource::ToneMappedTexture);

        std::vector<IRenderPass*> passes =
        {
            mPrefilterEnvMapPass.get(), mPrecomputeBRDFPass.get(), mClusteredPass.get(), mGBufferPass.get(),
            mDeferredShadingPass.get(), mSkyboxPass.get(), mAutoExposurePass.get(), mToneMappingPass.get(), mBloomPass.get(), mPresentPass.get()
        };

        return passes;
    }

    SkyboxPass::SkyboxPass()
    {
        static MeshData mesh = DefaultResource::StandardSphereMesh();

        mBoxIndexBuffer = GD3D12ResourceAllocator->CreateIndexBuffer(mesh.Indicies().GetData(), mesh.IndiciesCount() * sizeof(IndexType));
        mBoxVertexBuffer = GD3D12ResourceAllocator->CreateVertexBuffer(mesh.Vertices().GetData(), mesh.VerticesCount() * sizeof(Vertex<SkyBoxMeshFormat>), sizeof(Vertex<SkyBoxMeshFormat>));

        mShadingState.SetShader("skybox.hlsl", false);

        WriteResource(DeferredPipelineResource::DeferredShadingRT);
        WriteResource(DeferredPipelineResource::DepthStencil);
    }

    void SkyboxPass::Execute(FGContext* context)
    {
        auto sky_box = context->Scene->GetSkyBox();

        if (sky_box) 
        {
            PIXScope(context->CommandList, "Skybox Pass");
            mShadingState.SetTexture("SkyBox", sky_box->Resource());

            // pso
            static PipelineStateDesc state = PipelineStateDesc::Generate(true, false, ECullMode_None);
            context->CommandList->SetGraphicsPipelineState(SkyBoxMeshFormat, &state, &mPassPsoDesc, mShadingState.GetShader());

            // draw skybox
            context->CommandList->DrawMesh(&mShadingState, SkyBoxMeshFormat, mBoxVertexBuffer.get(), mBoxIndexBuffer.get(), 0, mBoxIndexBuffer->IndiciesCount());
        }
    }

    void PreFilterEnvMapPass::Execute(FGContext* context)
    {
        // generate prefilter cubemap of skybox
        // this pass only need to be executed one time
        if (!mReady) 
        {
            mReady = true;
            
            PIXScope(context->CommandList, "Precompute PrefilterEnvMap Pass");

            for (uint32 i = 0; i < PreFilterEnvMapMipsLevel; i++)
            {
                ShadingState& shading_state = mShadingState[i];
                shading_state.SetShader("env_map_gen.hlsl", true);
                shading_state.SetRWTextureArray("PrefilterEnvMap", mPrefilterEnvMap.get());
                if (context->Scene->GetSkyBox()) 
                {
                    shading_state.SetTexture("SkyBox", context->Scene->GetSkyBox()->Resource());
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

                context->CommandList->Dispatch(&mShadingState[i], thread_group_num, thread_group_num, NumCubeMapFaces);
            }
        }
    }

    void PrecomputeBRDFPass::Execute(FGContext* context)
    {
        if (!mReady)
        {
            mReady = true;

            PIXScope(context->CommandList, "Precompute BRDF Pass");

            constexpr uint32 ThreadGroupSize = 8;
            constexpr uint32 ThreadGroupCount = 512 / ThreadGroupSize;

            ConstantBuffer cbuffer =
            {
                .TextureResolution = TextureResolution
            };

            mShadingState.SetConstantBuffer(cbuffer);
            context->CommandList->Dispatch(&mShadingState, ThreadGroupCount, ThreadGroupCount, 1);
        }
    }
     
    void GBufferPass::Execute(FGContext* context)
    {
        PIXScope(context->CommandList, "Gbuffer Pass");

        FrustumVolume volume = FrustumVolume::FromMatrix(context->Camera->GetProjectionMatrix() * context->Camera->GetLocalSpaceMatrix());

        mCullingStatus = {};
        context->Scene->CullModel(volume,
            [&](SceneModel* model)
            {
                DrawModel(context->CommandList, model);
            }
        );

        mCullingStatus.NumCulled = context->Scene->GetMeshCount() - mCullingStatus.NumDrawCall;
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

            ConstantBufferInstance cb{};

            // copy shader parameters from parameter table to constant buffer
            material->ApplyShaderParameter(cb, shading_state->GetShader(), ConstantBufferInstance::SemanticName);
            cb.Model = obj->GetWorldMatrix(),
            cb.InvModel = obj->GetWorldMatrix().Inverse(),

            obj->GetConstantBuffer()->CommitData(cb);
            cmd->SetGrphicsConstant(EConstantBufferType_Instance, obj->GetConstantBuffer()->GetCurrendConstantBufferView());

            // setup PSO
            cmd->SetGraphicsPipelineState(mesh->GetVertexFormat(), &mPipelineStateDesc, &mPassPsoDesc, shading_state->GetShader());
            
            // issue drawcall
            const SubMeshData& sub_mesh = mesh->GetSubMeshes()[i];
            cmd->DrawMesh(shading_state, mesh->GetVertexFormat(), mesh->GetVertexBuffer(), mesh->GetIndexBuffer(), sub_mesh.Index, sub_mesh.IndicesCount);
        }
    }

    void DeferredShadingPass::Execute(FGContext* context)
    {
        PIXScope(context->CommandList, "Deferred Shading");

        // bind pass input to shader
        mShadingState.SetTexture(DeferredShadingShader::GBufferA, dynamic_cast<DeviceTexture*>(GetTransientResource(context, DeferredPipelineResource::GBufferA)));
        mShadingState.SetTexture(DeferredShadingShader::GBufferB, dynamic_cast<DeviceTexture*>(GetTransientResource(context, DeferredPipelineResource::GBufferB)));
        mShadingState.SetTexture(DeferredShadingShader::GBufferC, dynamic_cast<DeviceTexture*>(GetTransientResource(context, DeferredPipelineResource::GBufferC)));
        mShadingState.SetTexture(DeferredShadingShader::PrefilterEnvMap, dynamic_cast<DeviceTexture*>(GetTransientResource(context, DeferredPipelineResource::PrefilterEnvMap)));
        mShadingState.SetTexture(DeferredShadingShader::PrecomputeBRDF, dynamic_cast<DeviceTexture*>(GetTransientResource(context, DeferredPipelineResource::PrecomputeBRDF)));
        mShadingState.SetTexture(DeferredShadingShader::DepthStencil, dynamic_cast<DeviceTexture*>(GetTransientResource(context, DeferredPipelineResource::DepthStencil)));

        mShadingState.SetStructuredBuffer(DeferredShadingShader::Clusters, dynamic_cast<DeviceStructuredBuffer*>(GetTransientResource(context, DeferredPipelineResource::FrustumCluster)));
        mShadingState.SetStructuredBuffer(DeferredShadingShader::PointLights, dynamic_cast<DeviceStructuredBuffer*>(GetTransientResource(context, DeferredPipelineResource::PointLights)));

        // setup pso
        context->CommandList->SetStencilRef(0);
        context->CommandList->SetGraphicsPipelineState(EVertexFormat_P3F_T2F, &mPipelineStateDesc, &mPassPsoDesc, mShadingState.GetShader());
        context->CommandList->DrawScreen(&mShadingState);
    }

    void ClusteredPass::Execute(FGContext* context)
    {
        {
            PIXScope(context->CommandList, "Clustered Pass");

            // bind structured buffer to shader
            DeviceStructuredBuffer* sw_cluster = dynamic_cast<DeviceStructuredBuffer*>(GetTransientResource(context, DeferredPipelineResource::FrustumCluster));
            DeviceStructuredBuffer* sw_point_light = dynamic_cast<DeviceStructuredBuffer*>(GetTransientResource(context, DeferredPipelineResource::PointLights));

            mClusteredCompute.SetRWStructuredBuffer(ClusteredComputeShader::Clusters, sw_cluster);
            mClusteredCulling.SetRWStructuredBuffer(ClusteredCullingShader::Clusters, sw_cluster);
            mClusteredCulling.SetRWStructuredBuffer(ClusteredCullingShader::PointLights, sw_point_light);

            // frustum culling point lights
            ASSERT(context->Scene->GetLightCount() <= MaxSceneLights);

            FrustumVolume volume = FrustumVolume::FromMatrix(context->Camera->GetProjectionMatrix() * context->Camera->GetLocalSpaceMatrix());
            std::array<PointLight, MaxSceneLights> lights;

            int i = 0;
            context->Scene->CullLight(volume,
                [&](SceneLight* light)
                {
                    lights[i++] = PointLight
                    {
                        .Position = light->GetTranslation(),
                        .Color = light->GetColor(),
                        .Intensity = light->GetIntensity(),
                        .Attenuation = light->GetAttenuationCoefficients()
                    };
                }
            );

            // set shader constant buffer
            ClusteredComputeShader::ShaderConstant cbuffer
            {
                .NumLight = i
            };
            mClusteredCompute.SetConstantBuffer(cbuffer);
            mClusteredCulling.SetConstantBuffer(cbuffer);

            // commit lights data
            sw_point_light->Commit(&lights, sizeof(lights));

            // calculate AABB of each cluster
            context->CommandList->Dispatch(&mClusteredCompute, 1, 1, 1);

            // calculate the lights that intersect with each cluster
            context->CommandList->Dispatch(&mClusteredCulling, 1, 1, 1);
        }
    }
    
    void AutoExposurePass::Execute(FGContext* context)
    {
        PIXScope(context->CommandList, "Auto Exposure Pass");

        DeviceTexture2D* input_tex = dynamic_cast<DeviceTexture2D*>(GetTransientResource(context, DeferredPipelineResource::DeferredShadingRT));
        DeviceStructuredBuffer* histogram = dynamic_cast<DeviceStructuredBuffer*>(GetTransientResource(context, DeferredPipelineResource::LuminanceHistogram));
        DeviceStructuredBuffer* avg_luminance = dynamic_cast<DeviceStructuredBuffer*>(GetTransientResource(context, DeferredPipelineResource::AverageLuminance));

        if (!mAvarageLuminanceInitialized)
        {
            mAvarageLuminanceInitialized = true;

            int value = 0;
            avg_luminance->Commit(&value, sizeof(float));
        }

        {
            PIXScope(context->CommandList, "Luminance Histogram Pass");

            histogram->Resource()->TransitionBarrier(context->CommandList->GetCommandList(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            mLuminanceHistogramCompute.SetRWStructuredBuffer(LuminanceHistogramShader::LuminanceHistogram, histogram);
            mLuminanceHistogramCompute.SetTexture(LuminanceHistogramShader::LuminanceTexture, input_tex);
            mLuminanceHistogramCompute.SetConstantBuffer
            (
                LuminanceHistogramShader::ConstantBuffer
                {
                    .TextureWidth = input_tex->Width(),
                    .TextureHeight = input_tex->Height(),
                    .MinLogLuminance = MinLogLuminance,
                    .InvLogLuminanceRange = InvLogLuminanceRange,
                }
            );

            uint32 dispatch_size_x = CalculateDispatchSize(input_tex->Width(), HistogramComputeThreadGroupSize);
            uint32 dispatch_size_y = CalculateDispatchSize(input_tex->Height(), HistogramComputeThreadGroupSize);

            context->CommandList->Dispatch(&mLuminanceHistogramCompute, dispatch_size_x, dispatch_size_y, 1);
        }

        {
            PIXScope(context->CommandList, "Average Luminance Pass");

            histogram->Resource()->TransitionBarrier(context->CommandList->GetCommandList(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            avg_luminance->Resource()->TransitionBarrier(context->CommandList->GetCommandList(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            mAvarageLuminanceCompute.SetRWStructuredBuffer(AverageLuminanceShader::LuminanceHistogram, histogram);
            mAvarageLuminanceCompute.SetRWStructuredBuffer(AverageLuminanceShader::AverageLuminance, avg_luminance);
            mAvarageLuminanceCompute.SetConstantBuffer(AverageLuminanceShader::ConstantBuffer
                {
                    .PixelCount = input_tex->Width() * input_tex->Height(),
                    .MinLogLuminance = MinLogLuminance,
                    .LogLuminanceRange = LogLuminanceRange,
                }
                );

            context->CommandList->Dispatch(&mAvarageLuminanceCompute, 1, 1, 1);
        }
    }

    void ToneMappingPass::Execute(FGContext* context)
    {
        PIXScope(context->CommandList, "Tone Mapping Pass");

        DeviceTexture2D* input_tex = dynamic_cast<DeviceTexture2D*>(GetTransientResource(context, DeferredPipelineResource::DeferredShadingRT));
        DeviceStructuredBuffer* avg_luminance = dynamic_cast<DeviceStructuredBuffer*>(GetTransientResource(context, DeferredPipelineResource::AverageLuminance));

        avg_luminance->Resource()->TransitionBarrier(context->CommandList->GetCommandList(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        mToneMappingRender.SetRWStructuredBuffer(ToneMappingShader::AverageLuminance, avg_luminance);
        mToneMappingRender.SetTexture(ToneMappingShader::LuminanceTexture, input_tex);

        PipelineStateDesc pso_desc = PipelineStateDesc::DrawScreen();
        context->CommandList->SetGraphicsPipelineState(EVertexFormat_P3F_T2F, &pso_desc, &mPassPsoDesc, mToneMappingRender.GetShader());
        context->CommandList->DrawScreen(&mToneMappingRender);

    }

    BloomPass::BloomPass()
    {
        // declare pass input output
        const FGTransientTextureDescription& tex_desc = FGResourceDescriptionTable::Instance()->GetTransientTexture(DeferredPipelineResource::DeferredShadingRT);

        ASSERT(BloomStep < CalculateMaxMipLevels(tex_desc.Info.Width, tex_desc.Info.Height));

        WriteTransientTexture(DeferredPipelineResource::BloomMipchain, tex_desc.Info.Width, tex_desc.Info.Height, MipmapLevel, tex_desc.Info.Format, ETexture2DFlag_AllowUnorderedAccess);
        WriteTransientTexture(DeferredPipelineResource::BloomTempTexture, tex_desc.Info.Width, tex_desc.Info.Height, MipmapLevel, tex_desc.Info.Format, ETexture2DFlag_AllowUnorderedAccess);
        WriteResource(DeferredPipelineResource::DeferredShadingRT);

        // initialize shading state
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
    // 1 step meas a horizontal and a vertical process, example above has 2 steps, and the mipmap level equal steps + 1 + Prefilter step, which is 4.
    void BloomPass::Execute(FGContext* context)
    {
        PIXScope(context->CommandList, "Bloom Pass");

        DeviceTexture2D* original_tex = dynamic_cast<DeviceTexture2D*>(GetTransientResource(context, DeferredPipelineResource::DeferredShadingRT));
        DeviceTexture2D* mip_chain = dynamic_cast<DeviceTexture2D*>(GetTransientResource(context, DeferredPipelineResource::BloomMipchain));
        DeviceTexture2D* temp_tex = dynamic_cast<DeviceTexture2D*>(GetTransientResource(context, DeferredPipelineResource::BloomTempTexture));

        ASSERT(original_tex);

        // bloom prefilter
        {
            PIXScope(context->CommandList, "Bloom Prefilter");

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
            mPrefilter.SetRWTexture(PrefilterShader::OutputTexture, mip_chain, 1);
            context->CommandList->Dispatch(&mPrefilter, CalculateDispatchSize(temp_tex->Width(), PrefilterShader::ThreadGroupSizeX), CalculateDispatchSize(temp_tex->Height(), PrefilterShader::ThreadGroupSizeY), 1);
        }

        {
            PIXScope(context->CommandList, "Bloom Downsample");
            // bloom downsample
            for (int i = 0; i < BloomStep; i++)
            {
                uint32 upper_mip_level = i + 1;
                int32 lower_mip_width = temp_tex->Width() >> (upper_mip_level + 1);
                int32 lower_mip_height = temp_tex->Height() >> (upper_mip_level + 1);

                {
                    PIXScope(context->CommandList, "Blur Horizontal");

                    // use gaussian filter for downsampling
                    mDownsampleH[i].SetConstantBuffer
                    (
                        BlurHorizontal::ConstantBuffer
                        {
                            .TexelSize = Vector2(1.0f / lower_mip_width, 1.0f / lower_mip_height),
                        }
                    );

                    // horizontal blur
                    mDownsampleH[i].SetTexture(BlurHorizontal::InputTexture, mip_chain, upper_mip_level);
                    mDownsampleH[i].SetRWTexture(BlurHorizontal::OutputTexture, temp_tex, upper_mip_level + 1);
                    context->CommandList->Dispatch(&mDownsampleH[i], CalculateDispatchSize(lower_mip_width, BlurHorizontal::ThreadGroupSizeX), CalculateDispatchSize(lower_mip_height, BlurHorizontal::ThreadGroupSizeY), 1);
                }

                {
                    PIXScope(context->CommandList, "Blur Vertical");
                    mDownsampleV[i].SetConstantBuffer(BlurVerticalShader::ConstantBuffer
                        {
                            .TexelSize = Vector2(1.0f / lower_mip_width, 1.0f / lower_mip_height),
                        }
                    );

                    // vertical blur
                    mDownsampleV[i].SetTexture(BlurVerticalShader::InputTexture, temp_tex, i + 2);
                    mDownsampleV[i].SetRWTexture(BlurVerticalShader::OutputTexture, mip_chain, i + 2);
                    context->CommandList->Dispatch(&mDownsampleV[i], CalculateDispatchSize(lower_mip_width, BlurVerticalShader::ThreadGroupSizeX), CalculateDispatchSize(lower_mip_height, BlurVerticalShader::ThreadGroupSizeY), 1);
                }
            }
        }

        {
            PIXScope(context->CommandList, "Bloom Upsample");

            // guassain filter upper level and lower level and add them together
            // let 2d gaussain filter to be S, and 1d horizontal and vertical gaussian filter to be H and V
            // upslample process is like S(t1) + S(t2) = V(H(t1)) + V(H(t2)) = V(H(t1) + H(t2))
            for (int i = BloomStep - 1; i >= 0; i--)
            {
                int32 upper_mip_level = i + 1;
                int32 upper_mip_width = temp_tex->Width() >> upper_mip_level;
                int32 upper_mip_height = temp_tex->Height() >> upper_mip_level;

                {
                    PIXScope(context->CommandList, "Upsample Horizontal Add");

                    // H(t1) + H(t2), merge two mip levels
                    mUpsampleH[i].SetConstantBuffer
                    (
                        UpsampleAddShader::ConstantBuffer
                        {
                            .TexelSize = Vector2(1.0f / upper_mip_width, 1.0f / upper_mip_height),
                        }
                    );

                    mUpsampleH[i].SetTexture(UpsampleAddShader::UpperLevel, mip_chain, upper_mip_level);
                    mUpsampleH[i].SetTexture(UpsampleAddShader::LowerLevel, mip_chain, upper_mip_level + 1);
                    mUpsampleH[i].SetRWTexture(UpsampleAddShader::OutputTexture, temp_tex, i + 1);
                    context->CommandList->Dispatch(&mUpsampleH[i], CalculateDispatchSize(upper_mip_width, UpsampleAddShader::ThreadGroupSizeX), CalculateDispatchSize(upper_mip_height, UpsampleAddShader::ThreadGroupSizeY), 1);
                }

                {
                    PIXScope(context->CommandList, "Blur Vertical");
                    mUpsampleV[i].SetConstantBuffer
                    (
                        BlurVerticalShader::ConstantBuffer
                        {
                            .TexelSize = Vector2(1.0f / upper_mip_width, 1.0f / upper_mip_height),
                        }
                    );

                    // V(t)
                    mUpsampleV[i].SetTexture(BlurVerticalShader::InputTexture, temp_tex, upper_mip_level);
                    mUpsampleV[i].SetRWTexture(BlurVerticalShader::OutputTexture, mip_chain, upper_mip_level);
                    context->CommandList->Dispatch(&mUpsampleV[i], CalculateDispatchSize(upper_mip_width, BlurVerticalShader::ThreadGroupSizeX), CalculateDispatchSize(upper_mip_height, BlurVerticalShader::ThreadGroupSizeY), 1);
                }
            }
        }

        {
            PIXScope(context->CommandList, "Upsample Merge");
            // merge the blurred texture with the original texture
            // simply gaussain filter upsample and add the original texture

            uint32 upper_mip_width = temp_tex->Width();
            uint32 upper_mip_height = temp_tex->Height();
            ASSERT(upper_mip_width == mip_chain->Width() && upper_mip_height == mip_chain->Height());

            {
                PIXScope(context->CommandList, "Blur Horizontal");

                mUpsampleBlurH.SetConstantBuffer
                (
                    BlurHorizontal::ConstantBuffer
                    {
                        .TexelSize = Vector2(1.0f / upper_mip_width, 1.0f / upper_mip_height),
                    }
                );

                mUpsampleBlurH.SetTexture(BlurHorizontal::InputTexture, mip_chain, 1);
                mUpsampleBlurH.SetRWTexture(BlurHorizontal::OutputTexture, temp_tex, 0);
                context->CommandList->Dispatch(&mUpsampleBlurH, CalculateDispatchSize(upper_mip_width, BlurHorizontal::ThreadGroupSizeX), CalculateDispatchSize(upper_mip_height, BlurHorizontal::ThreadGroupSizeY), 1);
            }

            {
                PIXScope(context->CommandList, "Blur Vertical");
                mUpsampleBlurV.SetConstantBuffer
                (
                    BlurVerticalShader::ConstantBuffer
                    {
                        .TexelSize = Vector2(1.0f / upper_mip_width, 1.0f / upper_mip_height),
                    }
                );

                // vertical blur
                mUpsampleBlurV.SetTexture(BlurVerticalShader::InputTexture, temp_tex, 0);
                mUpsampleBlurV.SetRWTexture(BlurVerticalShader::OutputTexture, mip_chain, 0);
                context->CommandList->Dispatch(&mUpsampleBlurV, CalculateDispatchSize(upper_mip_width, BlurVerticalShader::ThreadGroupSizeX), CalculateDispatchSize(upper_mip_height, BlurVerticalShader::ThreadGroupSizeY), 1);
            }

            {
                PIXScope(context->CommandList, "Merge");

                // merge with original texture
                mUpsampleMerge.SetTexture(MergeShader::InputTexture , mip_chain, 0);
                mUpsampleMerge.SetRWTexture(MergeShader::OutputTexture, original_tex, 0);
                context->CommandList->Dispatch(&mUpsampleMerge, CalculateDispatchSize(original_tex->Width(), MergeShader::ThreadGroupSizeX), CalculateDispatchSize(original_tex->Height(), MergeShader::ThreadGroupSizeY), 1);
            }
        }
    }
}