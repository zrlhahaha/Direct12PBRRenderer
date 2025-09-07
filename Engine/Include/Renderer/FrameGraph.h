#pragma once
#include <stack>
#include "Renderer/Pipeline/IPipeline.h"
#include "Renderer/Device/Direct12/DeviceResource.h"

namespace MRenderer 
{
    class FGExecutionParser 
    {
    protected:
        struct Node
        {
            IRenderPass* Pass;
            std::vector<Node*> InputNodes;
            std::vector<Node*> OutputNodes;

            bool Visited;
            uint32 RefCount;
        };

        struct FGResourceLifecycle
        {
            FGResourceId ResourceId;
            uint32 StartPass;
            uint32 EndPass;
            bool Valid; // is this transient resource ever used
        };

    public:
        inline const std::vector<IRenderPass*>& GetExecutionOrder() const { return mExecutionOrder; }
        inline const std::vector<FGResourceLifecycle>& GetResourceLifecycle() const { return mResourceLifecycle; }

        void Parse(std::vector<IRenderPass*> passes, IRenderPass* present_pass);
        bool IsDependsOn(const IRenderPass* lhs, const IRenderPass* rhs);

    protected:
        std::vector<IRenderPass*> mExecutionOrder;
        std::vector<FGResourceLifecycle> mResourceLifecycle;
    };

    class FrameGraph 
    {
    public:
        FrameGraph(IRenderPipeline* pipeline)  
            : mRenderPipeline(pipeline), mExecutionPass(0)
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
        IDeviceResource* GetFGResource(IRenderPass* pass, FGResourceId id);

    protected:
        void PreparePass(D3D12CommandList* cmd, uint32 pass_index);
        void GeneratePassPSO(GraphicsPass* pass);

    protected:
        FGExecutionParser mParser;
        FGResourceAllocator mFGResourceAllocator;

        std::vector<IRenderPass*> mPipelinePasses;
        IRenderPipeline* mRenderPipeline;

        uint32 mExecutionPass;
    };
}