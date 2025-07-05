#pragma once
#include <vector>
#include <unordered_map>
#include <string>
#include <functional>

#include "Fundation.h"
#include "Utils/MathLib.h"
#include "Renderer/Device/Direct12/DeviceResource.h"
#include "Renderer/Pipeline/IPipeline.h"
#include "Resource/BasicStorage.h"

namespace MRenderer
{
    class D3D12ShaderProgram;

    enum EResourceFormat
    {
        EResourceFormat_None,
        EResourceFormat_Binary,
        EResourceFormat_Json,
    };

    inline std::string_view GetResourceExtension(EResourceFormat format)
    {
        if (format == EResourceFormat_Json)
        {
            return ".json";
        }
        else if (format == EResourceFormat_Binary)
        {
            return ".bin";
        }
        else
        {
            UNEXPECTED("Unknow Resource Format");
            return nullptr;
        }
    }

    class IResource
    {
    public:
        constexpr static EResourceFormat ResourceFormat = EResourceFormat_None;

    public:
        IResource()
        {
        }

        virtual ~IResource()
        {
        }

        inline std::string_view GetRepoPath() const{ return mRepoPath;}
        inline void SetRepoPath(std::string_view repo_path) { mRepoPath = repo_path;}

    public:
        // runtime member
        std::string mRepoPath;
    };

    class MeshResource : public IResource
    {
    public:
        constexpr static EResourceFormat ResourceFormat = EResourceFormat_Binary;

    public:
        MeshResource()
            :IResource()
        {
        }

        MeshResource(std::string_view repo_path, MeshData mesh_data);

        inline const MeshData& GetMeshData() const { return mMeshData; }
        DeviceVertexBuffer* GetVertexBuffer() { return mDeviceVertexBuffer.get();}
        DeviceIndexBuffer* GetIndexBuffer() { return mDeviceIndexBuffer.get();}
        inline const AABB& GetBound() const { return mMeshData.mBound; }

        void PostDeserialized();

    protected:
        void AllocateGPUResource();

    public:
        // serializable member
        MeshData mMeshData;

        // runtime member
        std::shared_ptr<DeviceVertexBuffer> mDeviceVertexBuffer = nullptr;
        std::shared_ptr<DeviceIndexBuffer> mDeviceIndexBuffer = nullptr;
    };


    class TextureResource : public IResource 
    {
    public:
        constexpr static EResourceFormat ResourceFormat = EResourceFormat_Binary;

    public:
        TextureResource()
            :IResource()
        {
        }

        TextureResource(std::string_view repo_path, TextureData data)
            : IResource(), mTextureData(std::move(data))
        {
            SetRepoPath(repo_path);
            AllocateGPUResource();
        }

        void PostDeserialized();

        inline DeviceTexture2D* Resource() 
        {
            ASSERT(mDeviceTexture);
            return mDeviceTexture.get();
        }

    protected:
        void AllocateGPUResource();

    public:
        // serializable member
        TextureData mTextureData;

        // runtime member
        std::shared_ptr<DeviceTexture2D> mDeviceTexture;
    };

    class CubeMapResource : public IResource 
    {
    public:
        constexpr static EResourceFormat ResourceFormat = EResourceFormat_Binary;

    public:
        CubeMapResource()
            :IResource()
        {
        }

        CubeMapResource(std::string_view repo_path, std::array<TextureData, 6> data)
            :IResource(), mTextureData(std::move(data))
        {
            SetRepoPath(repo_path);

            ASSERT(
                data[0].mInfo == data[1].mInfo && data[1].mInfo == data[2].mInfo &&
                data[2].mInfo == data[3].mInfo && data[3].mInfo == data[4].mInfo &&
                data[4].mInfo == data[5].mInfo
            );
            AllocateGPUResource();
            GenerateSHCoefficients();
        }

        void GenerateSHCoefficients();
        void PostDeserialized();

        inline DeviceTexture2DArray* Resource() { ASSERT(mDeviceTexture2DArray); return mDeviceTexture2DArray.get(); }
        inline const SH2CoefficientsPack& GetSHCoefficients() const { return mSHCoefficients;}

    protected:
        void AllocateGPUResource();

    public:
        // serializable member
        std::array<TextureData, 6> mTextureData;
        SH2CoefficientsPack mSHCoefficients;
        
        // runtime member
        std::shared_ptr<DeviceTexture2DArray> mDeviceTexture2DArray;
    };


    class ShadingState;
    class MaterialResource : public IResource 
    {
    public:
        constexpr static EResourceFormat ResourceFormat = EResourceFormat_Json;

    public:
        MaterialResource()
            :IResource()
        {
            mShadingState = std::make_unique<ShadingState>();
        }

        MaterialResource(std::string_view repo_path)
            :IResource()
        {
            mShadingState = std::make_unique<ShadingState>();
            SetRepoPath(repo_path);
        }

        void SetShader(std::string filename);
        void SetShaderParameter(std::string name, ShaderParameter val);
        void SetTexture(std::string_view semantic_name, std::string_view local_filepath);
        void SetTexture(std::string_view semantic_name, const std::shared_ptr<TextureResource>& texture_resource);

        inline ShadingState* GetShadingState() { return mShadingState.get(); };
        std::optional<ShaderParameter> GetShaderParameter(const std::string& name);

        void PostSerialized() const;
        void PostDeserialized();

        // copy shader parameters value in this material to @t
        // basically just query constant buffer reflection and do the memcpy
        template<typename T>
        void ApplyShaderParameter(T& t, D3D12ShaderProgram* program, std::string_view constant_buffer_name)
        {
            const ShaderConstantBufferAttribute* constant_buffer = program->GetPrimaryShader()->FindConstantBufferAttribute(constant_buffer_name);
            for (auto& it : mParameterTable)
            {
                const ShaderConstantBufferVarriable* var = constant_buffer->GetVarialbe(it.first);
                if (!var)
                {
                    Log(std::format("Unknow Shader Parameter: {}, Material File:{}", it.first, mRepoPath));
                    continue;
                }

                ASSERT((var->mOffset + var->mSize <= sizeof(T)) && "Inconsistant Constant Buffer Defination");

                std::visit(
                    [&](auto&& value)
                    {
                        void* var_addr = reinterpret_cast<uint8*>(&t) + var->mOffset;
                        memcpy(var_addr, &value, var->mSize);
                    }, 
                    it.second.mData
                );
            }
        }

    public:
        // serializable member
        std::string mShaderPath;
        std::unordered_map<std::string, std::string> mTexturePath;
        std::unordered_map<std::string, ShaderParameter> mParameterTable;
        
        // runtime member
        std::vector<std::shared_ptr<TextureResource>> mTextureRefs;
        std::unique_ptr<ShadingState> mShadingState;
    };

    class ModelResource : public IResource
    {
    public:
        constexpr static EResourceFormat ResourceFormat = EResourceFormat_Json;

    public:
        ModelResource()
            :IResource()
        {
        }

        ModelResource(std::string_view filepath, const std::shared_ptr<MeshResource>& mesh_resource, const std::vector<std::shared_ptr<MaterialResource>>& material_resource)
            :IResource()
        {
            SetRepoPath(filepath);
            SetMesh(mesh_resource);
            for (uint32 i = 0; i < material_resource.size(); i++) 
            {
                SetMaterial(i, material_resource[i]);
            }
        }

        inline MaterialResource* GetMaterial(uint32 index) { return mMaterials[index].get();}
        inline const AABB& GetBound() const { return mMeshResource->GetBound(); }
        inline MeshResource* GetMeshResource() { return mMeshResource.get(); }
        
        void PostSerialized() const;
        void PostDeserialized();

        void SetMaterial(uint32 index, const std::shared_ptr<MaterialResource>& res);
        void SetMesh(const std::shared_ptr<MeshResource>& res);

    public:
        // serializable member
        std::string mMeshPath;
        std::vector<std::string> mMaterialPath;

        // runtime member
        std::shared_ptr<MeshResource> mMeshResource;
        std::vector<std::shared_ptr<MaterialResource>> mMaterials;
    };
}