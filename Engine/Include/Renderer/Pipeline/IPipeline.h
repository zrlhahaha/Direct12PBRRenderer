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
#include "Resource/json.hpp"
#include "Renderer/FrameGraphResource.h"


namespace MRenderer 
{
    class Scene;
    class Camera;
    class D3D12CommandList;

    enum EConstantBufferType
    {
        EConstantBufferType_Shader = 0,
        EConstantBufferType_Instance = 1,
        EConstantBufferType_Global = 2,
        EConstantBufferType_Total = 3
    };

    // dummy class
    struct ConstantBufferShader 
    {
    public:
        static constexpr std::string_view SemanticName = ConstantBufferShaderSemanticName;
    };

    struct ConstantBufferGlobal
    {
    public:
        static constexpr std::string_view SemanticName = ConstantBufferGlobalSemanticName;

    public:
        SH2CoefficientsPack SkyBoxSH;

        // matrix is stored in row major order on both the CPU side (Matrix4x4) and the GPU side (cbuffer)
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
        float DeltaTime;
        float Time;
    };

    struct ConstantBufferInstance
    {
    public:
        static constexpr std::string_view SemanticName = ConstantBufferInstanceSemanticName;

    public:
        ConstantBufferInstance() 
            :Albedo(1.0f, 1.0f, 1.0f), Emission(0.0f), Roughness(1.0f), Metallic(0.0f), UseAlbedoMap(false),
            UseNormalMap(false), UseMetallicMap(false), UseRoughnessMap(false), UseAmbientOcclusionMap(false)
        {
        }

    public:
        Matrix4x4 Model;
        Matrix4x4 InvModel;

        Vector3 Albedo;
        float Emission;
        float Roughness;
        float Metallic;

        BOOL UseAlbedoMap;
        BOOL UseNormalMap;
        BOOL UseMetallicMap;
        BOOL UseRoughnessMap;
        BOOL UseAmbientOcclusionMap;
    };

    struct ShaderParameter
    {
    public:
        using StorageType = std::variant<bool, float, Vector2, Vector3, Vector4>;

    public:
        ShaderParameter()
        {
            mData = 0.0f;
        }

        template<typename T>
        ShaderParameter(const T& val)
        {
            mData = val;
        }

        template<typename T>
        T& Value()
        {
            return std::get<T>(mData);
        }

        static void JsonSerialize(nlohmann::json& json, const ShaderParameter& t);
        static void JsonDeserialize(nlohmann::json& json, ShaderParameter& t);

    public:
        StorageType mData;
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
        bool SetTexture(std::string_view semantic_name, DeviceTexture2D* texture, uint32 mip_slice);
        bool SetRWTexture(std::string_view semantic_name, DeviceTexture2D* texture);
        bool SetRWTexture(std::string_view semantic_name, DeviceTexture2D* texture, uint32 mip_slice);
        bool SetRWTextureArray(std::string_view semantic_name, DeviceTexture2DArray* texture);
        bool SetStructuredBuffer(std::string_view semantic_name, DeviceStructuredBuffer* buffer);
        bool SetRWStructuredBuffer(std::string_view semantic_name, DeviceStructuredBuffer* buffer);
        
        void ClearResourceBinding();
        D3D12ShaderProgram* GetShader();
        const ResourceBinding* GetResourceBinding() const;
        DeviceConstantBuffer* GetConstantBuffer();

        template<typename T>
        void SetConstantBuffer(const T& t) 
        {
            mShaderConstantBuffer->CommitData(t);
        }

        friend void swap(ShadingState& lhs, ShadingState& rhs) 
        {
            using std::swap;
            swap(lhs.mResourceBinding, rhs.mResourceBinding);
            swap(lhs.mShaderProgram, rhs.mShaderProgram);
            swap(lhs.mShaderConstantBuffer, rhs.mShaderConstantBuffer);
        }

    protected:
        const ShaderAttribute* FindShaderAttribute(EShaderAttrType type, std::string_view semantic_name);

    protected:
        ResourceBinding mResourceBinding;
        D3D12ShaderProgram* mShaderProgram;
        bool mIsCompute;
        std::shared_ptr<DeviceConstantBuffer> mShaderConstantBuffer;
    };

    class IRenderPass
    {
        friend class FrameGraph;
        friend class RenderScheduler;
    public:
        IRenderPass() = default;
        virtual ~IRenderPass() {};

        IRenderPass(const IRenderPass&) = delete;
        IRenderPass(IRenderPass&&) = delete;

        IRenderPass& operator=(const IRenderPass&) = delete;
        IRenderPass& operator=(IRenderPass&&) = delete;

        inline const std::vector<FGResourceId>& GetInputResources() const { return mInputResources; }
        inline const std::vector<FGResourceId>& GetOutputResources() const { return mOutputResources; }

    protected:
        inline void ReadResource(FGResourceId id) 
        { 
            ASSERT(std::find(mInputResources.begin(), mInputResources.end(), id) == mInputResources.end());
            mInputResources.push_back(id);
        }

        inline void WriteResource(FGResourceId id) 
        {
            ASSERT(std::find(mOutputResources.begin(), mOutputResources.end(), id) == mOutputResources.end());
            mOutputResources.push_back(id);
        }

        // declare this pass will read or write to certain resources
        inline void WriteTransientTexture(FGResourceId id, uint32 width, uint32 height, uint32 mip_levels, ETextureFormat format, ETexture2DFlag flag=ETexture2DFlag_AllowRenderTarget)
        {
            FGResourceDescriptionTable::Instance()->DeclareTransientTexture(id, width, height, mip_levels, format, flag);
            WriteResource(id);
        }

        inline void WriteTransientBuffer(FGResourceId id, uint32 size, uint32 stride)
        {
            FGResourceDescriptionTable::Instance()->DeclareTransientBuffer(id, size, stride);
            WriteResource(id);
        }

        inline void WritePersistentResource(FGResourceId id, IDeviceResource* res)
        {
            FGResourceDescriptionTable::Instance()->DeclarePersistentResource(id, res);
            WriteResource(id);
        }

        IDeviceResource* GetTransientResource(FGContext* context, FGResourceId id);

        virtual void Execute(FGContext* context) = 0;

    protected:
        // the number and format of render target and depth stencil that this pass is gonna write
        // it's matained by @FrameGraph
        GraphicsPassPsoDesc mPassState;

        std::vector<FGResourceId> mInputResources;
        std::vector<FGResourceId> mOutputResources;
    };

    class PresentPass : public IRenderPass
    {
    public:
        static constexpr std::string_view Input_FinalTexture = "InputTexture";

    public:
        PresentPass()
            :mFinalTexture(InvalidFGResourceId)
        {
        }

        void Execute(FGContext* context) override;
        void SetFinalTexture(FGResourceId id);
    protected:
        FGResourceId mFinalTexture;
    };

    class GraphicsPass : public IRenderPass 
    {
    public:
        GraphicsPass() 
            : IRenderPass(), mPassPsoDesc{}
        {
        }

        inline const GraphicsPassPsoDesc& GetPsoDesc() const { return mPassPsoDesc; }
        inline void SetPsoDesc(const GraphicsPassPsoDesc& pso_desc) { mPassPsoDesc = pso_desc; }

    protected:
        GraphicsPassPsoDesc mPassPsoDesc;
    };

    class ComputePass : public IRenderPass 
    {
    };

    class IRenderPipeline 
    {
        friend class FrameGraph;
    public:
        IRenderPipeline()
        {
            mPresentPass = std::make_unique<PresentPass>();
        }
        virtual ~IRenderPipeline() {}

        IRenderPipeline(const IRenderPipeline&) = delete;
        IRenderPipeline& operator=(const IRenderPipeline&) = delete;

        virtual std::vector<IRenderPass*> Setup() = 0;
        virtual FrustumCullStatus GetStatus() const { return FrustumCullStatus{}; };

    protected:
        std::unique_ptr<PresentPass> mPresentPass;
    };
}