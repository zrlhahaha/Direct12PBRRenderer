#include "Renderer/RenderScheduler.h"
#include "Renderer/Scene.h"


namespace MRenderer 
{
    RenderScheduler::RenderScheduler(IRenderPipeline* pipeline)
        :mFrameGraph(nullptr)
    {
        mGlobalConstantBuffer = GD3D12Device->CreateConstBuffer(sizeof(ConstantBufferGlobal));
        mCommandList = std::make_unique<D3D12CommandList>(GD3D12RawDevice);

        SetupPipeline(pipeline);
    }

    D3D12CommandList* RenderScheduler::ExecutePipeline(Scene* scene, Camera* camera)
    {
        mCommandList->BeginFrame();

        mGlobalConstantBuffer->CommitData(
            ConstantBufferGlobal{
                .SkyBoxSH = scene->GetSkyBox() ? scene->GetSkyBox()->GetSHCoefficients() : SH2CoefficientsPack{},
                .InvView = camera->GetWorldMatrix(),
                .View = camera->GetLocalSpaceMatrix(),
                .Projection = camera->GetProjectionMatrix(),
                .InvProjection = camera->GetProjectionMatrix().Inverse(),
                .CameraPos = camera->GetTranslation(),
                .Ratio = camera->Ratio(),
                .Resolution = Vector2(static_cast<float>(GD3D12Device->Width()), static_cast<float>(GD3D12Device->Height())),
                .Near = camera->Near(),
                .Far = camera->Far(),
                .Fov = camera->Fov(),
            }
        );
        mCommandList->SetGrphicsConstant(EConstantBufferType_Global, mGlobalConstantBuffer->GetCurrendConstantBufferView());

        mFrameGraph->Execute(mCommandList.get(), scene, camera);

        // present render target
        PresentPass* present_pass = mFrameGraph->GetPipeline()->GetPresentPass();
        ASSERT(present_pass->GetRenderTargetNodes().size() > 0);

        RenderPassNode* res = present_pass->IndexRenderTarget(0);
        DeviceTexture2D* rt = dynamic_cast<DeviceTexture2D*>(res->GetResource());
        ASSERT(rt);

        mCommandList->Present(rt);
        mCommandList->EndFrame();
        return mCommandList.get();
    }

    void RenderScheduler::SetupPipeline(IRenderPipeline* pipeline)
    {
        mFrameGraph = std::make_unique<FrameGraph>(pipeline);

        // process frame graph
        mFrameGraph->Setup();
        mFrameGraph->Compile();
    }
    FrustumCullStatus RenderScheduler::GetStatus() const
    {
        return mFrameGraph->GetPipeline()->GetStatus();
    }
}
