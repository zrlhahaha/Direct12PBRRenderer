#pragma once
#include <vector>
#include <map>

#include "Renderer/Device/Direct12/DeviceResource.h"
#include "Utils/MathLib.h"
#include "Utils/SH.h"
#include "Utils/Misc.h"
#include "Fundation.h"
#include "Utils/Thread.h"
#include "Resource/Shader.h"
#include "Resource/VertexLayout.h"


namespace MRenderer 
{
    class Scene;
    class Camera;
    class D3D12CommandList;

    union RenderTargetKey
    {
        struct
        {
            uint16 Width;
            uint16 Height;
            ETextureFormat Format;
            uint8 Padding1 = 0;
            uint16 Padding2 = 0;
        } Info;
        uint64 Key;

        RenderTargetKey() 
            :Key(0)
        {
        }

        RenderTargetKey(uint16 width, uint16 height, ETextureFormat format)
            :Info{ width, height, format }
        {
        }

        RenderTargetKey(const RenderTargetKey& other) 
            :Key(other.Key)
        {
        }

        RenderTargetKey& operator= (RenderTargetKey & other)
        {
            Key = other.Key;
            return *this;
        }

        bool operator==(const RenderTargetKey& other) const
        {
            return Key == other.Key;
        }
    };

    static_assert(sizeof(RenderTargetKey) == 8);
}

template <>
struct std::hash<MRenderer::RenderTargetKey>
{
    std::size_t operator()(const MRenderer::RenderTargetKey& k) const
    {
        return k.Key;
    }
};

namespace MRenderer
{
#ifdef false
    enum ERenderItemType : uint8
    {
        ERenderItemType_DrawInstance = 0,
        ERenderItemType_DrawScreen = 1,
        ERenderItemType_Compute = 2,
    };

    struct RenderItem
    {
        ERenderItemType Type;

        // mesh info
        EVertexFormat VertexFormat;
        const DeviceVertexBuffer* Vertices;
        const DeviceIndexBuffer* Indicies;
        uint32 IndexBegin;
        uint32 IndexCount;

        // textures and buffers
        const ResourceBinding* ResourceBinding;
        const D3D12ShaderProgram* Program;
        DeviceConstantBuffer* ShaderConstantBuffer;
        DeviceConstantBuffer* InstanceConstantBuffer;

        // thread group size
        uint32 ThreadGroupCountX;
        uint32 ThreadGroupCountY;
        uint32 ThreadGroupCountZ;

        static RenderItem DrawInstance(EVertexFormat format, const DeviceVertexBuffer* vertices, const DeviceIndexBuffer* indices, uint32 index_begin, uint32 index_count, ShadingState* shading_state, DeviceConstantBuffer* object_constant_buffer)
        {
            return RenderItem
            {
                .Type = ERenderItemType_DrawInstance,
                .VertexFormat = format,
                .Vertices = vertices,
                .Indicies = indices,
                .IndexBegin = index_begin,
                .IndexCount = index_count,
                .ResourceBinding = &shading_state->GetResourceBinding(),
                .Program = shading_state->GetShader(),
                .ShaderConstantBuffer = shading_state->GetConstantBuffer(),
                .InstanceConstantBuffer = object_constant_buffer,
            };
        }
        static RenderItem DrawScreen(ShadingState* shading_state)
        {
            return RenderItem
            {
                .Type = ERenderItemType_DrawScreen,
                .ResourceBinding = &shading_state->GetResourceBinding(),
                .Program = shading_state->GetShader(),
                .ShaderConstantBuffer = shading_state->GetConstantBuffer(),
            };
        }
        static RenderItem Compute(ShadingState* shading_state, uint32 thread_group_x, uint32 thread_group_y, uint32 thread_group_z)
        {
            return RenderItem
            {
                .Type = ERenderItemType_Compute,
                .ResourceBinding = &shading_state->GetResourceBinding(),
                .Program = shading_state->GetShader(),
                .ShaderConstantBuffer = shading_state->GetConstantBuffer(),
                .ThreadGroupCountX = thread_group_x,
                .ThreadGroupCountY = thread_group_y,
                .ThreadGroupCountZ = thread_group_z,
            };
        }
    };
#endif

    enum EConstantBufferType
    {
        EConstantBufferType_Shader = 0,
        EConstantBufferType_Instance = 1,
        EConstantBufferType_Global = 2,
        EConstantBufferType_Total = 3
    };

    struct alignas(16) ConstantBufferGlobal
    {
        SH2CoefficientsPack SkyBoxSH;
        Matrix4x4 InvView;
        Matrix4x4 View;
        Matrix4x4 Projection;
        Matrix4x4 InvProjection;
        Vector3 CameraPos;
        float Ratio;
        Vector2 Resolution;
        float Near;
        float Far;
        float Fov;
    };

    struct ConstantBufferInstance
    {
        Matrix4x4 Model;
        Matrix4x4 InvModel;
    };

    // a class contains shader, texturex, constant buffer for a draw call
    class ShadingState
    {
    public:
        ShadingState();

        ShadingState(const ShadingState& other) = delete;
        ShadingState(ShadingState&& other);
        ShadingState& operator=(ShadingState other);

        void SetShader(std::string_view shader_file_path, bool is_compute);
        bool SetTexture(std::string_view semantic_name, DeviceTexture* texture);
        bool SetRWTexture(std::string_view semantic_name, DeviceTexture2D* texture);
        bool SetRWTextureArray(std::string_view semantic_name, DeviceTexture2DArray* texture);
        bool SetStructuredBuffer(std::string_view semantic_name, DeviceStructuredBuffer* buffer);
        bool SetRWStructuredBuffer(std::string_view semantic_name, DeviceStructuredBuffer* buffer);
        
        void ClearResourceBinding();
        const D3D12ShaderProgram* GetShader() const;
        const ResourceBinding* GetResourceBinding() const;
        DeviceConstantBuffer* GetConstantBuffer();

        template<typename T>
        void SetConstantBuffer(const T& t) 
        {
            mShaderConstantBuffer->CommitData(t);
        }

        friend void Swap(ShadingState& lhs, ShadingState& rhs) 
        {
            using std::swap;
            swap(lhs.mResourceBinding, rhs.mResourceBinding);
            swap(lhs.mShaderProgram, rhs.mShaderProgram);
            swap(lhs.mShaderConstantBuffer, rhs.mShaderConstantBuffer);
        }

    protected:
        ResourceBinding mResourceBinding;
        D3D12ShaderProgram* mShaderProgram;
        bool mIsCompute;
        std::shared_ptr<DeviceConstantBuffer> mShaderConstantBuffer;
    };

    enum ERenderPassNodeType 
    {
        ERenderPassNodeType_Transient = 0, // reference to a transient RT
        ERenderPassNodeType_Reference = 1, // reference to a transient RT holds by other node
        ERenderPassNodeType_Persisten = 2, // reference to a persisten RT
    };


    class IRenderPass;
    struct RenderPassNode
    {
    public:
        ETextureFormat Format();
        IDeviceResource* GetResource();
        RenderPassNode* GetActualPassNode();

    public:
        static std::unique_ptr<RenderPassNode> PersistentPassResource(std::string_view name, IRenderPass* pass, IDeviceResource* tex)
        {
            return std::unique_ptr<RenderPassNode>(
                new RenderPassNode{
                    .Name = name.data(),
                    .Pass = pass,
                    .Type = ERenderPassNodeType_Persisten,
                    .PersistentResource = tex,
                }
            );
        }

        static std::unique_ptr<RenderPassNode> TransientPassResource(std::string_view name, IRenderPass* pass, RenderTargetKey key)
        {
            return std::unique_ptr<RenderPassNode>(
                new RenderPassNode{
                    .Name = name.data(),
                    .Pass = pass,
                    .Type = ERenderPassNodeType_Transient,
                    .TransientResource = 
                        {
                            .RenderTargetKey = key,
                        }
                }
            );
        }

        static std::unique_ptr<RenderPassNode> TransientPassResourceReference(std::string_view name, IRenderPass* pass, RenderPassNode* node)
        {
            return std::unique_ptr<RenderPassNode>(
                new RenderPassNode{
                    .Name = name.data(),
                    .Pass = pass,
                    .Type = ERenderPassNodeType_Reference,
                    .PassNodeReference = node,
                }
            );
        }

    public:
        std::string Name;
        IRenderPass* Pass = nullptr;
        uint32 RefCount = 0; // used for transient resource lifetime tracking
        ERenderPassNodeType Type = ERenderPassNodeType_Transient;

        union 
        {
            // for persisten resource
            IDeviceResource* PersistentResource;
            // for transient resource, refer to a trasient resource in RenderTargetPool
            struct 
            {
                IDeviceResource* Resource;
                RenderTargetKey RenderTargetKey;
                uint32 Index;
            } TransientResource;
            
            // for transient resource reference, refer to transient RT belongs to another RenderPassNode
            RenderPassNode* PassNodeReference;
        };
    };

    class IRenderPass
    {
        friend class FrameGraph;
        friend class RenderScheduler;
    public:
        IRenderPass() = default;
        virtual ~IRenderPass() {};

        inline RenderPassNode* IndexRenderTarget(uint32 index) { return mRenderTargets[index]; }
        inline RenderPassNode* GetDepthStencil() { return mDepthStencil; }
        inline const std::array<RenderPassNode*, MaxRenderTargets>& GetRenderTargetNodes() const { return mRenderTargets; }
        inline uint32 GetRenderTargetsSize() const{ return mNumRenderTargets; }

        // use other render pass's output texture as shader input texture
        void SampleTexture(std::string_view semantic_name, RenderPassNode* node);

        // create transient RT and write to it
        void WriteRenderTarget(std::string_view name, RenderTargetKey key);

        // write to previous pass's output RT
        void WriteRenderTarget(std::string_view name, RenderPassNode* node);

        // create transient depth_stencil and write to it
        void WriteDepthStencil(uint32 width, uint32 height);

        // write to previous pass's depth_stencil
        void WriteDepthStencil(RenderPassNode* node);

        // delcare a persisten resource as pass output
        void WritePersistent(std::string_view name, IDeviceResource* persisten_resource);

        RenderPassNode* FindOutput(std::string_view name); 

        const RenderPassStateDesc* GetPassStateDesc() const;
        void BindRenderPassInput(ShadingState* shading_state);                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 

    protected:
        virtual void PostCompile() {};
        virtual void Execute(D3D12CommandList* cmd, Scene* scene, Camera* camera) {};

        void UpdatePassStateDesc();

    protected:
        // the number and format of render target and depth stencil that this pass is gonna write
        // it's matained by @FrameGraph
        RenderPassStateDesc mPassState;

        // pass execution orders depends on mInputNodes and mOutputNodes
        // mOutputNodes contains RenderTarget and DepthStencil that are gonna be writed by this pass
        // these resources might belong to this pass, or referecned from other pass
        std::vector<std::unique_ptr<RenderPassNode>> mOutputNodes;
        
        // mInputNodes contains other pass's RenderTarget and DepthStencil that are gonna be sampled by this pass
        std::vector<RenderPassNode*> mInputNodes;
        
        // corresponding shader semantics name for shader input textures
        std::vector<std::string> mInputSemantics;
        
        // render targets for this pass, use WriteRenderTarget to add one
        std::array<RenderPassNode*, MaxRenderTargets> mRenderTargets;
        // depth-stencil for this pass use WriteDepthStencil to add one
        RenderPassNode* mDepthStencil = nullptr;

        uint8 mNumRenderTargets = 0;
        uint8 mRefCount = 0; // for topology sort
        bool mSearchVisited = false; // for search effectivce pass
        bool mSortVisited = false; // for topology sort
    };

    class PresentPass : public IRenderPass
    {
    public:
        PresentPass()
        {
        }

        void Connect(RenderPassNode* node)
        {
            WriteRenderTarget("PresentRT", node);
        }
    };

    class IRenderPipeline 
    {
        friend class FrameGraph;
    public:
        IRenderPipeline()
        {
            mPresentPass = std::make_unique<PresentPass>();
        }

        IRenderPipeline(const IRenderPipeline&) = delete;
        IRenderPipeline& operator=(const IRenderPipeline&) = delete;

        virtual ~IRenderPipeline() {}
        
        inline PresentPass* GetPresentPass() { return mPresentPass.get();}

        virtual void Setup() = 0;
        virtual FrustumCullStatus GetStatus() const { return FrustumCullStatus{}; };

    protected:
        std::unique_ptr<PresentPass> mPresentPass;
    };
}