#pragma once
#include <vector>
#include <unordered_map>
#include <string>
#include <functional>

#include "Fundation.h"
#include "Utils/MathLib.h"
#include "Utils/Misc.h"
#include "VertexLayout.h"
#include "Renderer/Device/Direct12/DeviceResource.h"
#include "Renderer/Pipeline/IPipeline.h"
#include "json.hpp"

namespace MRenderer
{
    class D3D12ShaderProgram;

    using IndexType = uint32;

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

    class BinaryData 
    {
    public:
        BinaryData();
        BinaryData(uint32 size);
        BinaryData(const void* src_ptr, uint32 size);
        BinaryData(const BinaryData& other) = delete;
        BinaryData(BinaryData&& other);
        ~BinaryData();

        BinaryData& operator=(BinaryData other);

        inline void* GetData() const
        {
            return mData;
        }

        inline uint32 GetSize() const
        {
            return mSize;
        }

        inline void Reset() 
        {
            BinaryData temp = std::move(*this);
        }

        friend void Swap(BinaryData& lhs, BinaryData& rhs) 
        {
            using std::swap;

            swap(lhs.mSize, rhs.mSize);
            swap(lhs.mData, rhs.mData);
        }

        static void Serialize(RingBuffer&rb, const BinaryData& binary);
        static void Deserialize(RingBuffer& rb, BinaryData& out);

    protected:
        uint32 mSize;
        void* mData;
    };

    struct SubMeshData
    {
        static constexpr SubMeshData Whole(uint32 indices_count)
        {
            return SubMeshData{ 0, indices_count };
        }

        uint32 Index = 0;
        uint32 IndicesCount = 0;
    };

    class MeshData
    {
    public:
        MeshData()
            :mVertexFormat(EVertexFormat_None)
        {
        }

        MeshData(EVertexFormat vertex_format, BinaryData vertices, BinaryData indicies, const AABB& bound)
            :mVertexFormat(vertex_format), mBound(bound), mVertices(std::move(vertices)), mIndicies(std::move(indicies)), mSubMeshes{SubMeshData::Whole(indicies.GetSize() / sizeof(IndexType))}
        {
        }

        MeshData(EVertexFormat vertex_format, BinaryData vertices, BinaryData indicies, std::vector<SubMeshData> sub_meshes, const AABB& bound) 
            :mVertexFormat(vertex_format), mBound(bound), mVertices(std::move(vertices)), mIndicies(std::move(indicies)), mSubMeshes(sub_meshes)
        {
        }

        template<typename VertexType>
        MeshData(EVertexFormat vertex_format, std::vector<VertexType>& vertices, std::vector<uint32>& indices, std::vector<SubMeshData> sub_meshes, const AABB& bound)
            :mVertexFormat(vertex_format), mBound(bound), mSubMeshes(sub_meshes)
        {
            mVertices = BinaryData(vertices.data(), vertices.size() * sizeof(VertexType));
            mIndicies = BinaryData(indices.data(), indices.size() * sizeof(uint32));
        }

        template<typename VertexType>
        MeshData(EVertexFormat vertex_format, std::vector<VertexType>& vertices, std::vector<uint32>& indices, const AABB& bound)
            :mVertexFormat(vertex_format), mBound(bound), mSubMeshes{ SubMeshData::Whole(indices.size()) }
        {
            mVertices = BinaryData(vertices.data(), vertices.size() * sizeof(VertexType));
            mIndicies = BinaryData(indices.data(), indices.size() * sizeof(uint32));
        }

        ~MeshData() = default;

        MeshData(const MeshData&) = delete;
        MeshData(MeshData&&);
        MeshData& operator=(MeshData);

        inline const void* VerticesData() const 
        {
            return mVertices.GetData();
        }

        inline size_t VerticesCount() const
        {
            return mVertices.GetSize() / VertexStride();
        }

        inline size_t VertexStride() const
        {
            return GetVertexLayout(mVertexFormat).VertexSize;
        }

        inline const uint32* IndiciesData() const
        {
            return reinterpret_cast<uint32*>(mIndicies.GetData());
        }

        inline size_t IndiciesCount() const
        {
            return mIndicies.GetSize() / sizeof(uint32);
        }

        inline uint32 GetSubMeshCount() const 
        {
            return mSubMeshes.size();
        }

        inline const SubMeshData& GetSubMesh(uint32 index) const
        {
            return mSubMeshes[index];
        }

        inline const std::vector<SubMeshData>& GetSubMeshData() const 
        {
            return mSubMeshes;
        }

        inline EVertexFormat GetFormat() const
        {
            return mVertexFormat;
        }

        friend void Swap(MeshData& lhs, MeshData& rhs) 
        {
            using std::swap;
            swap(lhs.mVertexFormat, rhs.mVertexFormat);
            swap(lhs.mIndicies, rhs.mIndicies);
            swap(lhs.mVertices, rhs.mVertices);
            swap(lhs.mSubMeshes, rhs.mSubMeshes);
            swap(lhs.mBound, rhs.mBound);
        }

    public:
        EVertexFormat mVertexFormat;
        AABB mBound;
        BinaryData mVertices;
        BinaryData mIndicies;
        std::vector<SubMeshData> mSubMeshes;
    };

    struct TextureInfo 
    {
        uint16 Width;
        uint16 Height;
        ETextureFormat Format;

        bool operator==(const TextureInfo& other) const = default;
    };

    class TextureData
    {
    public:
        TextureData() 
            :mInfo{ .Width = 0, .Height = 0, .Format = ETextureFormat_None }
        {
        }

        TextureData(uint16 height, uint16 width, ETextureFormat format)
            :mInfo{.Width = width, .Height = height, .Format = format}, mData(TextureSize())
        {
            ASSERT((height % 4) == 0 && (width % 4) == 0);
        }

        TextureData(BinaryData binary_data, uint16 height, uint16 width, ETextureFormat format)
            :
            mInfo{.Width = width, .Height = height, .Format = format} , mData(std::move(binary_data))
        {
            // BC1 requires the width and height must be a multiple of 4
            ASSERT((height % 4) == 0 && (width % 4) == 0);
            ASSERT(TextureSize() == mData.GetSize());
        }

        ~TextureData() = default;

        TextureData(const TextureData& other) = delete;
        TextureData(TextureData&& other);
        TextureData& operator=(TextureData other);

        // sample pixel on (u, v)
        Vector4 Sample(float u, float v) const;
        // set pixel on (u, v)
        void SetPixel(uint32 u, uint32 v, const Vector4& color);
        uint32 PixelSize() const;
        
        inline uint32 TextureSize() const{ return mInfo.Width * mInfo.Height * PixelSize();}
        inline uint32 ChannelCount() const { return GetChannelCount(mInfo.Format);}
        inline ETextureFormat Format() const { return mInfo.Format; }
        inline uint32 Width() const { return mInfo.Width; }
        inline uint32 Height() const { return mInfo.Height; }

        friend void Swap(TextureData& lhs, TextureData& rhs)
        {
            Swap(lhs.mData, rhs.mData);
            std::swap(lhs.mInfo, rhs.mInfo);
        }

    public:
        static void Serialize(RingBuffer& rb, const TextureData& texture_data);
        static void Deserialize(RingBuffer& rb, TextureData& out_texture_data);

        // theta: angle between y-axis phi: angle between x-axis
        static Vector4 SampleTextureCube(const std::array<TextureData, 6>& data, float theta, float phi);

    public:
        TextureInfo mInfo;
        BinaryData mData;
    };

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

    struct ShaderParameter
    {
        ShaderParameter()
            : mType(EShaderParameter_Vec1)
        {
        }

    public:
        static void JsonSerialize(nlohmann::json& json, const ShaderParameter& t);
        static void JsonDeserialize(nlohmann::json& json, ShaderParameter& t);

    protected:
        enum EShaderParameter : uint8
        {
            EShaderParameter_Vec1,
            EShaderParameter_Vec2,
            EShaderParameter_Vec3,
            EShaderParameter_Vec4,
            EShaderParameter_Total,
        } mType;

        union ParameterData
        {
            float vec1;
            Vector2 vec2;
            Vector3 vec3;
            Vector4 vec4;

            ParameterData()
                : vec4{}
            {
            }
        } mData;
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

        void PostSerialized() const;
        void PostDeserialized();

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
        inline const AABB& GetBound() const { return mMeshResource->mMeshData.mBound; }
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