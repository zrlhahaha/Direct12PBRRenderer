#include "Renderer/Device/Direct12/D3D12Device.h"
#include "Renderer/FrameGraph.h"
#include "Renderer/Device/Direct12/D3D12CommandList.h"

namespace MRenderer
{
    // todo: frame allocator and custom allocator for std container
    void FrameGraph::Setup()
    {
        // connect passes
        mRenderPipeline->Setup();

        CleanUp();

        // DFS from mPresentPass, collect all effective pass
        std::stack<IRenderPass*> pass_stack;
        std::vector<IRenderPass*> effective_pass;
        pass_stack.push(mRenderPipeline->GetPresentPass());

        while (!pass_stack.empty())
        {
            IRenderPass* pass = pass_stack.top();
            pass_stack.pop();

            effective_pass.push_back(pass);
            for (IRenderPass* pass : CollectPassDependency(pass)) 
            {
                if (!pass->mSearchVisited) 
                {
                    pass->mSearchVisited = true;
                    pass_stack.push(pass);
                }

                pass->mRefCount++;
            }
        }

        // get pass execution order by topology sort from effective pass
        while (effective_pass.size() != mPassOrder.size())
        {
            size_t count = mPassOrder.size();

            for (IRenderPass* pass : effective_pass)
            {
                if (!pass->mSortVisited && pass->mRefCount == 0) 
                {
                    pass->mSortVisited = true;
                    mPassOrder.push_back(pass);

                    for (IRenderPass* deps : CollectPassDependency(pass)) 
                    {
                        deps->mRefCount--;
                    }
                }
            }

            ASSERT(count != mPassOrder.size() && "Pass Mutual Referenced");
        }

        std::reverse(mPassOrder.begin(), mPassOrder.end());

        // generate render pass state
        for (IRenderPass* pass : mPassOrder)
        {
            pass->UpdatePassStateDesc();
        }
    }
    
    void FrameGraph::Compile()
    {
        // add reference count if RenderPassResource is referenced by other pass
        for (IRenderPass* pass : mPassOrder)
        {
            for (RenderPassNode* input_node : pass->mInputNodes)
            {
                IncreseRef(input_node);
            }

            for (auto& output_node : pass->mOutputNodes)
            {
                IncreseRef(output_node.get());
            }
        }

        // calculate transient render target ID for each RenderPassResource
        for (int pass_index = 0; pass_index < mPassOrder.size(); pass_index++)
        {
            IRenderPass* pass = mPassOrder[pass_index];

            // assign transient RT for each output RTs
            for (uint32 node_index = 0; node_index < pass->mOutputNodes.size(); node_index++)
            {
                RenderPassNode* res = pass->mOutputNodes[node_index].get();
                if (res->Type == ERenderPassNodeType_Transient) 
                {
                    ASSERT(res->TransientResource.Resource == nullptr && res->TransientResource.RenderTargetKey != TextureFormatKey());

                    uint32 index = mRenderTargetPool.Allocate(res->TransientResource.RenderTargetKey);
                    res->TransientResource.Resource = mRenderTargetPool.GetResource(res->TransientResource.RenderTargetKey, index);
                    res->TransientResource.Index = index;
                }
            }

            // recycle transient RT for each output RTs if the are not referenced by other passes at all
            for (uint32 node_index = 0; node_index < pass->mOutputNodes.size(); node_index++)
            {
                RenderPassNode* res = pass->mOutputNodes[node_index].get();
                DecreseRef(res);
            }

            // recycle transient RT for input transient RTs if they are no longer being referenced
            for (uint32 node_index = 0; node_index < pass->mInputNodes.size(); node_index++)
            {
                RenderPassNode* res = pass->mInputNodes[node_index];
                DecreseRef(res);
            }
        }
    }

    void FrameGraph::Execute(D3D12CommandList* cmd, Scene* scene, Camera* camera)
    {
        for (uint32 i = 0; i < mPassOrder.size(); i++)
        {
            PreparePass(cmd, mPassOrder[i]);
            mPassOrder[i]->Execute(cmd, scene, camera);
        }
    }

    void FrameGraph::PreparePass(D3D12CommandList* cmd, IRenderPass* pass)
    {
        // collect and clean up render target resource
        const std::array<RenderPassNode*, MaxRenderTargets>& rt_nodes = pass->GetRenderTargetNodes();
        std::array<RenderTargetView*, MaxRenderTargets> rtv_array = {};
        for (uint32 i = 0; i < pass->GetRenderTargetsSize(); i++)
        {
            DeviceRenderTarget* rt = dynamic_cast<DeviceRenderTarget*>(rt_nodes[i]->GetResource());
            ASSERT(rt);
            RenderTargetView* rtv = rtv_array[i] = rt->GetRenderTargetView();

            // if it's the begining of a transient RT, clean it up first
            if (rt_nodes[i]->GetActualPassNode()->Pass == pass)
            {
                cmd->ClearRenderTarget(rtv);
            }
        }

        // collect and clean up depth stencil resource
        RenderPassNode* depth_stencil_node = pass->GetDepthStencil();
        DepthStencilView* dsv = nullptr;
        if (depth_stencil_node)
        {
            DeviceDepthStencil* depth_stencil = dynamic_cast<DeviceDepthStencil*>(depth_stencil_node->GetResource());
            dsv = depth_stencil ? depth_stencil->GetDepthStencilView() : nullptr;

            // if it's the begining of a transient depth stencil, clean it up first
            if (depth_stencil_node->GetActualPassNode()->Pass == pass)
            {
                cmd->ClearDepthStencil(dsv);
            }
        }

        // bind RTs and depth stencil
        cmd->SetRenderTarget(rtv_array, pass->GetRenderTargetsSize(), dsv);
    }

    void FrameGraph::IncreseRef(RenderPassNode* node)
    {
        node = node->GetActualPassNode();
        if (node->Type == ERenderPassNodeType_Transient)
        {
            node->RefCount += 1;
        }
    }

    void FrameGraph::DecreseRef(RenderPassNode* node)
    {
        node = node->GetActualPassNode();
        if (node->Type == ERenderPassNodeType_Transient)
        {
            if (--node->RefCount == 0)
            {
                ASSERT(node->TransientResource.Resource != nullptr);
                mRenderTargetPool.Free(node->TransientResource.RenderTargetKey, node->TransientResource.Index);
            }
        }
    }

    void FrameGraph::CleanUp()
    {
        for (IRenderPass* pass : mPassOrder) 
        {
            pass->mSearchVisited = false;
            pass->mSortVisited = false;
            pass->mPassState = {};
            pass->mRefCount = 0;

            for (auto& node : pass->mOutputNodes) 
            {
                node->RefCount = 0;

                if (node->Type == ERenderPassNodeType_Transient)
                {
                    node->TransientResource.Resource = nullptr;
                    node->TransientResource.Index = 0;
                }
            }
        }

        mPassOrder.clear();
        mRenderTargetPool.Reset();
    }

    std::vector<IRenderPass*> FrameGraph::CollectPassDependency(IRenderPass* pass)
    {
        std::vector<IRenderPass*> deps;

        // if pass A will sample pass B's RT, then pass B is referenced by pass A
        for (RenderPassNode* input_node : pass->mInputNodes)
        {
            if (input_node->Pass)
            {
                deps.push_back(input_node->Pass);
            }
        }

        // if pass A will write to pass B's RT, then pass B is referenced by pass A
        for (auto& output_node : pass->mOutputNodes)
        {
            if (output_node->Type == ERenderPassNodeType_Reference)
            {
                RenderPassNode* node = output_node->PassNodeReference;
                ASSERT(node->Pass);
                deps.push_back(node->Pass);
            }
        }

        deps.erase(std::unique(deps.begin(), deps.end()), deps.end());
        return deps;
    }

    uint32 TransientTexturePool::Allocate(TextureFormatKey key)
    {
        std::vector<Item>& items = mTransientResources[key];

        for (uint32 i = 0; i < items.size(); i++)
        {
            if (!items[i].Occupied)
            {
                items[i].Occupied = true;
                return static_cast<uint32>(mTransientResources[key].size() - 1);
            }
        }

        if (key.Info.Format != ETextureFormat_DepthStencil)
        {
            auto render_target = GD3D12Device->CreateRenderTarget(key.Info.Width, key.Info.Height, key.Info.Format);
            size_t index = mTransientResources[key].size();
            render_target->Resource()->SetName((std::wstring(L"TransientRenderTarget") + std::to_wstring(index)).data());

            mTransientResources[key].emplace_back(true, render_target);
            return static_cast<uint32>(index);
        }
        else
        {
            auto depth_stencil = GD3D12Device->CreateDepthStencil(key.Info.Width, key.Info.Height);
            size_t index = mTransientResources[key].size();
            depth_stencil->Resource()->SetName((std::wstring(L"TransientDepthStencil") + std::to_wstring(index)).data());

            mTransientResources[key].emplace_back(true, depth_stencil);
            return static_cast<uint32>(index);
        }
    }

    void TransientTexturePool::Free(TextureFormatKey key, uint32 index)
    {
        mTransientResources[key][index].Occupied = false;
    }

    void TransientTexturePool::Reset()
    {
        for (auto& vec : mTransientResources)
        {
            for (auto& res : vec.second)
            {
                res.Occupied = false;
            }
        }
    }

    IDeviceResource* TransientTexturePool::GetResource(TextureFormatKey key, uint32 index)
    {
        return mTransientResources[key][index].Resource.get();
    }
}
