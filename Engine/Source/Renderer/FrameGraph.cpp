#include "Renderer/Device/Direct12/D3D12Device.h"
#include "Renderer/FrameGraph.h"
#include "Renderer/Device/Direct12/D3D12CommandList.h"

namespace MRenderer
{
    // todo: frame allocator and custom allocator for std container
    void FrameGraph::Setup()
    {
        // create and connect passes 
        mPipelinePasses = mRenderPipeline->Setup();

        // generate render pass PSO description
        for (IRenderPass* pass : mPipelinePasses)
        {
            GraphicsPass* graphics_pass = dynamic_cast<GraphicsPass*>(pass);

            if (!graphics_pass) // not for compute pass
                continue;

            GeneratePassPSO(graphics_pass);
        }
    }
    
    void FrameGraph::Compile()
    {
        // determine pass execution order and transient resource lifecycle
        mParser.Parse(mPipelinePasses, mRenderPipeline->mPresentPass.get());

        // create transient resource
        mFGResourceAllocator.Reset();

        for (auto& lifecycle : mParser.GetResourceLifecycle()) 
        {
            if (lifecycle.Valid) 
            {
                mFGResourceAllocator.AllocateTransientResource(lifecycle.ResourceId);
            }
        }
    }

    void FrameGraph::Execute(D3D12CommandList* cmd, Scene* scene, Camera* camera)
    {
        FGContext context = 
        {
            .CommandList = cmd,
            .Scene = scene,
            .Camera = camera,
            .FrameGraph = this
        };

        // execute each pass accroding to the resource dependency order
        const std::vector<IRenderPass*> execution_order = mParser.GetExecutionOrder();

        for (mExecutionPass = 0; mExecutionPass < execution_order.size(); mExecutionPass++)
        {
            PreparePass(cmd, mExecutionPass);
            execution_order[mExecutionPass]->Execute(&context);
        }
    }

    IDeviceResource* FrameGraph::GetFGResource(IRenderPass* pass, FGResourceId id)
    {
        ASSERT(mParser.GetExecutionOrder()[mExecutionPass] == pass);
        ASSERT
        (
            std::find(pass->GetInputResources().begin(), pass->GetInputResources().end(), id) != pass->GetInputResources().end() ||
            std::find(pass->GetOutputResources().begin(), pass->GetOutputResources().end(), id) != pass->GetOutputResources().end()
        );

        Overload overloads
        {
            [&](const FGTransientTextureDescription& res) -> IDeviceResource*
            {
                return mFGResourceAllocator.GetResource(id);
            },
            [&](const FGTransientBufferDescription& res) -> IDeviceResource*
            {
                return mFGResourceAllocator.GetResource(id);
            },
            [&](const FGPersistentResourceDescription& res) -> IDeviceResource*
            {
                return FGResourceDescriptionTable::Instance()->GetPersistentResource(id).Resource;
            }
        };

        IDeviceResource* res = FGResourceDescriptionTable::Instance()->VisitResourceDescription(id, overloads);
        ASSERT(res);

        return res;
    }

    // clean up and bind render target
    void FrameGraph::PreparePass(D3D12CommandList* cmd, uint32 pass_index)
    {
        GraphicsPass* pass = dynamic_cast<GraphicsPass*>(mParser.GetExecutionOrder()[pass_index]);

        // ignore compute pass
        if (!pass)
            return;

        DepthStencilView* dsv = nullptr;
        std::array<RenderTargetView*, MaxRenderTargets> rtv_array = {};
        uint32 num_rts = 0;

        for (FGResourceId res_id : pass->GetOutputResources())
        {
            DeviceTexture2D* texture = dynamic_cast<DeviceTexture2D*>(GetFGResource(pass, res_id));
            ASSERT(texture);

            bool begin_pass = mParser.GetResourceLifecycle()[res_id].StartPass == pass_index;

            if (texture->TextureFlag() & ETexture2DFlag_AllowRenderTarget) 
            {
                // for rtv
                ASSERT(num_rts <= rtv_array.size() && "render targetr exceeds the maximum limitation");
                rtv_array[num_rts++] = texture->GetRenderTargetView();

                // clear rt if it's the beginning of it's lifecycle
                if (begin_pass) 
                {
                    cmd->ClearRenderTarget(texture->GetRenderTargetView());
                }
            }
            else if (texture->TextureFlag() & ETexture2DFlag_AllowDepthStencil)
            {
                // for dsv
                ASSERT(dsv == nullptr && "try to bind multiple depth stencil to one pass");
                dsv = texture->GetDepthStencilView();

                // clear rt if it's the beginning of it's lifecycle
                if (begin_pass) 
                {
                    cmd->ClearDepthStencil(texture->GetDepthStencilView());
                }
            }
        }

        // bind RTs and depth stencil
        cmd->SetRenderTarget(rtv_array, num_rts, dsv);
    }

    void FrameGraph::GeneratePassPSO(GraphicsPass* pass)
    {
        GraphicsPassPsoDesc pso_desc = {};

        auto record_output = [&](ETextureFormat format, ETexture2DFlag flag)
        {
            // it's either a render target or depth stencil, d3d12 doesn't allow both exist.
            if (flag & ETexture2DFlag::ETexture2DFlag_AllowRenderTarget)
            {
                pso_desc.RenderTargetFormats[pso_desc.NumRenderTarget++] = format;
            }
            else if (flag & ETexture2DFlag::ETexture2DFlag_AllowDepthStencil)
            {
                ASSERT(pso_desc.DepthStencilFormat == ETextureFormat_None);
                pso_desc.DepthStencilFormat = format;
            }
        };

        Overload overloads 
        (
            [=](const FGTransientTextureDescription& desc)
            {
                record_output(desc.Info.Format, desc.Info.Flag);
            },
            [=](const FGPersistentResourceDescription& desc)
            {
                DeviceTexture2D* tex = dynamic_cast<DeviceTexture2D*>(desc.Resource);

                if (tex)
                {
                    record_output(tex->Format(), tex->TextureFlag());
                }
            },
            [](const FGTransientBufferDescription& desc) // warn: do not capture unused variable here, msvc bug will cause stack corruption
            {
                // graphics pass won't write to buffers
                ASSERT(false);
            }
        );

        for (FGResourceId output_res : pass->GetOutputResources())
        {
            FGResourceDescriptionTable::Instance()->VisitResourceDescription(output_res, overloads);
        }

        pass->SetPsoDesc(pso_desc);
    }

    void FGExecutionParser::Parse(std::vector<IRenderPass*> passes, IRenderPass* present_pass)
    {
        mExecutionOrder.clear();

        // initialize nodes
        Node* final_pass = nullptr;
        std::vector<Node> nodes;
        for (auto& pass : passes)
        {
            Node node = { .Pass = pass };
            nodes.push_back(node);
        }

        // link nodes by resource dependency
        for (Node& lhs : nodes)
        {
            for (Node& rhs : nodes)
            {
                if (IsDependsOn(lhs.Pass, rhs.Pass))
                {
                    rhs.RefCount++;
                    lhs.InputNodes.push_back(&rhs);
                    rhs.OutputNodes.push_back(&lhs);
                }
            }

            if (lhs.Pass == present_pass)
            {
                final_pass = &lhs;
            }
        }

        ASSERT(final_pass && final_pass->RefCount == 0);

        // find out execution order by topological sort
        std::stack<Node*> dfs_stack;
        dfs_stack.push(final_pass);

        while (!dfs_stack.empty())
        {
            Node* node = dfs_stack.top();
            dfs_stack.pop();

            mExecutionOrder.push_back(node->Pass);

            for (Node* dependency : node->InputNodes)
            {
                dependency->RefCount -= 1;

                if (!dependency->Visited && dependency->RefCount == 0)
                {
                    dependency->Visited = true;
                    dfs_stack.push(dependency);
                }
            }
        }

        ASSERT(passes.size() == mExecutionOrder.size() && "unused pass or circular reference is found in the frame graph");

        std::reverse(mExecutionOrder.begin(), mExecutionOrder.end());

        // determine transient resource lifecycle
        mResourceLifecycle.resize(FGResourceIDs::Instance()->NumResources());
        for (int32 i = 0; i < static_cast<int32>(FGResourceIDs::Instance()->NumResources()); i++)
        {
            mResourceLifecycle[i] = { .ResourceId = i, .Valid = false };
        }

        for (uint32 i = 0; i < mExecutionOrder.size(); i++) 
        {
            IRenderPass* pass = mExecutionOrder[i];

            auto extend_lifecycle = [&](FGResourceId res)
            {
                auto& lifecycle = mResourceLifecycle[res];

                if (lifecycle.Valid)
                {
                    lifecycle.StartPass = Min(lifecycle.StartPass, i);
                    lifecycle.EndPass = Max(lifecycle.EndPass, i);
                }
                else
                {
                    lifecycle.Valid = true;
                    lifecycle.StartPass = i;
                    lifecycle.EndPass = i;
                }
            };

            for (FGResourceId input_res : pass->GetInputResources()) 
            {
                extend_lifecycle(input_res);
            }

            for (FGResourceId output_res : pass->GetOutputResources())
            {
                extend_lifecycle(output_res);
            }
        }
    }
    
    bool FGExecutionParser::IsDependsOn(const IRenderPass* lhs, const IRenderPass* rhs)
    {
        if (lhs == rhs)
        {
            return false;
        }

        for (const FGResourceId& input : lhs->GetInputResources())
        {
            for (const FGResourceId& output : rhs->GetOutputResources())
            {
                if (input == output)
                {
                    return true;
                }
            }
        }

        return false;
    }
}
