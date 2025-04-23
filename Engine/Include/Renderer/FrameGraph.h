#pragma once
#include <stack>
#include "Renderer/Device/Direct12/DeviceResource.h"
#include "Renderer/Pipeline/IPipeline.h"

namespace MRenderer 
{
    class TransientTexturePool
    {
        struct Item
        {
            bool Occupied = false;
            std::shared_ptr<IDeviceResource> Resource = nullptr;
        };

    public:
        // allocate transient resource, could be render target and depth stencil depend on texture format
        uint32 Allocate(RenderTargetKey key);
        void Free(RenderTargetKey key, uint32 index);
        void Reset();
        IDeviceResource* GetResource(RenderTargetKey key, uint32 index);

    protected:
        std::unordered_map<RenderTargetKey, std::vector<Item>> mTransientResources;
    };

    class FrameGraph 
    {
    public:
        FrameGraph(IRenderPipeline* pipeline)  
            : mRenderPipeline(pipeline)
        {
        }

        FrameGraph(const FrameGraph&) = delete;
        FrameGraph& operator=(FrameGraph&&) = delete;

        // construct each pass and calculate the pass execution order
        void Setup();

        // calculate each trasient RT's life time and allocate the resource
        void Compile();

        // execute each pass by the execute order
        void Execute(D3D12CommandList* cmd, Scene* scene, Camera* camera);

        IRenderPipeline* GetPipeline() const{ return mRenderPipeline;}
        const std::vector<IRenderPass*>& GetPassOrder() const{ return mPassOrder;}
    protected:
        void IncreseRef(RenderPassNode* node);
        void DecreseRef(RenderPassNode* node);
        void CleanUp();
        std::vector<IRenderPass*> CollectPassDependency(IRenderPass* pass);
        void PreparePass(D3D12CommandList* cmd, IRenderPass* pass);

    protected:
        std::vector<IRenderPass*> mPassOrder;

        TransientTexturePool mRenderTargetPool;
        IRenderPipeline* mRenderPipeline;
    };
}