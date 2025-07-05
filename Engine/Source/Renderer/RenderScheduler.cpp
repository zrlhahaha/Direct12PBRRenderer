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

    D3D12CommandList* RenderScheduler::ExecutePipeline(Scene* scene, Camera* camera, GameTimer* timer)
    {
        mCommandList->BeginFrame();

        if (scene) 
        {
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
                    .DeltaTime = timer->DeltaTime(),
                    .Time = timer->TotalTime(),
                }
            );

            mCommandList->SetGrphicsConstant(EConstantBufferType_Global, mGlobalConstantBuffer->GetCurrendConstantBufferView());
            mCommandList->SetComputeConstant(EConstantBufferType_Global, mGlobalConstantBuffer->GetCurrendConstantBufferView());

            mFrameGraph->Execute(mCommandList.get(), scene, camera);
        }
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
