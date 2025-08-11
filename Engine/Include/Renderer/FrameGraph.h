#pragma once
#include <stack>
#include "Renderer/Pipeline/IPipeline.h"
#include "Renderer/Device/Direct12/DeviceResource.h"
#include "Renderer/Device/Direct12/MemoryAllocator.h"

namespace MRenderer 
{
    class RTHandle 
    {
    public:

    protected:
        AllocationDesc mDesc;
        std::string mName;
        IDeviceResource* mResource;
    };

    class FrameGraph 
    {
    public:
        FrameGraph(IRenderPipeline* pipeline)  
            : mRenderPipeline(pipeline), mHeap(GD3D12RawDevice)
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



    protected:
        void IncreseRef(RenderPassNode* node);
        void DecreseRef(RenderPassNode* node);
        void CleanUp();
        std::vector<IRenderPass*> CollectPassDependency(IRenderPass* pass);
        void PreparePass(D3D12CommandList* cmd, IRenderPass* pass);

    protected:
        std::vector<IRenderPass*> mPassOrder;

        D3D12Memory::MultiHeapMemoryAllocator mHeap;
        IRenderPipeline* mRenderPipeline;
    };
}