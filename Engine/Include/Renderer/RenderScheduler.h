#pragma once
#include "Renderer/Device/Direct12/D3D12CommandList.h"
#include "Renderer/FrameGraph.h"
#include "Renderer/Camera.h"
#include "Utils/Time/GameTimer.h"

namespace MRenderer 
{
    class RenderScheduler 
    {
    public:
        RenderScheduler(IRenderPipeline* pipeline);
        RenderScheduler(const RenderScheduler&) = delete;
        RenderScheduler& operator=(const RenderScheduler&) = delete;

        D3D12CommandList* ExecutePipeline(Scene* scene, Camera* camera, GameTimer* timer);
        void SetupPipeline(IRenderPipeline* pipeline);
        FrustumCullStatus GetStatus() const;

    protected:
        std::unique_ptr<D3D12CommandList> mCommandList;
        std::unique_ptr<FrameGraph> mFrameGraph;
        std::shared_ptr<DeviceConstantBuffer> mGlobalConstantBuffer;
    };
}