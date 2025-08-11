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
        MeshResource()
            :IResource(), mVertexFormat(EVertexFormat_None)
        {
        }

        explicit MeshResource(std::string_view repo_path, std::string_view mesh_path);

        inline DeviceVertexBuffer* GetVertexBuffer() { return mDeviceVertexBuffer.get();}
        inline DeviceIndexBuffer* GetIndexBuffer() { return mDeviceIndexBuffer.get();}
        inline const EVertexFormat GetVertexFormat() const { return mVertexFormat; }
        inline const AABB& GetBound() const { return mBound; }
        inline const std::vector<SubMeshData>& GetSubMeshes() { return mSubMeshes; }

        void PostDeserialized();

    protected:
        void AllocateGPUResource();

    public:
        // serializable member
        std::string mMeshPath;

        // runtime member
        std::shared_ptr<DeviceVertexBuffer> mDeviceVertexBuffer = nullptr;
        std::shared_ptr<DeviceIndexBuffer> mDeviceIndexBuffer = nullptr;
        
        EVertexFormat mVertexFormat;
        AABB mBound;
        std::vector<SubMeshData> mSubMeshes;
    };


    class TextureResource : public IResource 
    {
    public:
        TextureResource()
            :IResource()
        {
        }

        TextureResource(std::string_view repo_path, std::string_view texture_path)
            : IResource(), mTexturePath(texture_path)
        {
            SetRepoPath(repo_path);
            AllocateGPUResource();
        }

        void PostDeserialized();
        inline DeviceTexture2D* Resource() { ASSERT(mDeviceTexture); return mDeviceTexture.get(); }

    protected:
        void AllocateGPUResource();

    public:
        // serializable member
        std::string mTexturePath;

        // runtime member
        std::shared_ptr<DeviceTexture2D> mDeviceTexture;
    };

    class CubeMapResource : public IResource 
    {
    public:
        CubeMapResource()
            :IResource()
        {
        }

        CubeMapResource(std::string_view repo_path, std::string_view texture_data_path)
            :IResource(), mTexturePath(texture_data_path)
        {
            SetRepoPath(repo_path);

            CubeMapTextureData texture = ReadTextureFile();
            AllocateGPUResource(texture);
        }

        void PostDeserialized();

        inline DeviceTexture2DArray* Resource() { ASSERT(mDeviceTexture2DArray); return mDeviceTexture2DArray.get(); }
        inline const SH2CoefficientsPack& GetSHCoefficients() const { return mSHCoefficients;}
        CubeMapTextureData ReadTextureFile();

    protected:
        void AllocateGPUResource(const CubeMapTextureData& texture);

    public:
        // serializable member
        std::string mTexturePath;
        
        // runtime member
        std::shared_ptr<DeviceTexture2DArray> mDeviceTexture2DArray;
        SH2CoefficientsPack mSHCoefficients;
    };


    class ShadingState;
    class MaterialResource : public IResource 
    {
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