#pragma once
#include "Resource/json.hpp"
#include "Resource/VertexLayout.h"
#include "Utils/Misc.h"

namespace MRenderer
{
    // same as DXGI_FORMAT
    // note: TextureDate::BinarySerialze assume the format between [DXGI_FORMAT_R32G32B32A32_TYPELESS(1), DXGI_FORMAT_R32G32_SINT(18)] is HDR image
    // and will use DXGI_FORMAT_BC6H_UF16 to compress the image
    enum ETextureFormat : uint8
    {
        ETextureFormat_None = 0,
        ETextureFormat_R32G32B32A32_TYPELESS = 1,
        ETextureFormat_R32G32B32A32_FLOAT = 2,
        ETextureFormat_R16G16B16A16_FLOAT = 10,
        ETextureFormat_R16G16B16A16_UNORM = 11,
        ETextureFormat_R32G32_SINT = 18,
        ETextureFormat_FORMAT_R10G10B10A2_UNORM = 24,
        ETextureFormat_R8G8B8A8_UNORM = 28,
        ETextureFormat_R16G16_FLOAT = 34,
        ETextureFormat_R16G16_UNORM = 35,
        ETextureFormat_R8G8_UNORM = 49,
        ETextureFormat_R8_UNORM = 61,
        ETextureFormat_DepthStencil = 100,
    };

    uint32 GetChannelCount(ETextureFormat format);
    uint32 GetPixelSize(ETextureFormat format);
    uint32 GetPixelSize(DXGI_FORMAT format);

    class BinaryData
    {
    public:
        BinaryData();
        explicit BinaryData(uint32 size);
        explicit BinaryData(const void* src_ptr, uint32 size);
        
        BinaryData(const BinaryData& other) = delete;
        BinaryData(BinaryData&& other);
        BinaryData& operator=(BinaryData other);
        
        ~BinaryData();


        inline const void* GetData() const { return mData; }
        inline void* GetData() { return mData; }
        inline uint32 GetSize() const { return mSize; }
        inline uint32 Empty() const { return mData == nullptr; }
        inline void Reset() { BinaryData temp = std::move(*this); }
        inline const void* Offset(uint32 offset) const 
        {
            ASSERT(offset < mSize && "out of range");
            return static_cast<const uint8*>(mData) + offset;
        }

        friend void Swap(BinaryData& lhs, BinaryData& rhs)
        {
            using std::swap;

            swap(lhs.mSize, rhs.mSize);
            swap(lhs.mData, rhs.mData);
        }

        static void BinarySerialize(RingBuffer& rb, const BinaryData& binary);
        static void BinaryDeserialize(RingBuffer& rb, BinaryData& out);

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

    using IndexType = uint32;

    class MeshData
    {
    public:
        MeshData()
            :mVertexFormat(EVertexFormat_None)
        {
        }

        explicit MeshData(EVertexFormat vertex_format, BinaryData vertices, BinaryData indicies, const AABB& bound)
            :mVertexFormat(vertex_format), mBound(bound), mVertices(std::move(vertices)), mIndicies(std::move(indicies)), mSubMeshes{ SubMeshData::Whole(indicies.GetSize() / sizeof(IndexType)) }
        {
        }

        explicit MeshData(EVertexFormat vertex_format, BinaryData vertices, BinaryData indicies, std::vector<SubMeshData> sub_meshes, const AABB& bound)
            :mVertexFormat(vertex_format), mBound(bound), mVertices(std::move(vertices)), mIndicies(std::move(indicies)), mSubMeshes(sub_meshes)
        {
        }

        template<typename VertexType>
        explicit MeshData(EVertexFormat vertex_format, std::vector<VertexType>& vertices, std::vector<uint32>& indices, std::vector<SubMeshData> sub_meshes, const AABB& bound)
            : mVertexFormat(vertex_format), mBound(bound), mSubMeshes(sub_meshes)
        {
            mVertices = BinaryData(vertices.data(), static_cast<uint32>(vertices.size() * sizeof(VertexType)));
            mIndicies = BinaryData(indices.data(), static_cast<uint32>(indices.size() * sizeof(uint32)));
        }

        template<typename VertexType>
        explicit MeshData(EVertexFormat vertex_format, std::vector<VertexType>& vertices, std::vector<uint32>& indices, const AABB& bound)
            :mVertexFormat(vertex_format), mBound(bound), mSubMeshes{ SubMeshData::Whole(static_cast<uint32>(indices.size())) }
        {
            mVertices = BinaryData(vertices.data(), static_cast<uint32>(vertices.size() * sizeof(VertexType)));
            mIndicies = BinaryData(indices.data(), static_cast<uint32>(indices.size() * sizeof(uint32)));
        }

        ~MeshData() = default;

        MeshData(const MeshData&) = delete;
        MeshData(MeshData&&);
        MeshData& operator=(MeshData);

        inline const void* VerticesData() const
        {
            return mVertices.GetData();
        }

        inline uint32 VerticesCount() const
        {
            return static_cast<uint32>(mVertices.GetSize() / VertexStride());
        }

        inline uint32 VertexStride() const
        {
            return static_cast<uint32>(GetVertexLayout(mVertexFormat).VertexSize);
        }

        inline const uint32* IndiciesData() const
        {
            return reinterpret_cast<const uint32*>(mIndicies.GetData());
        }

        inline uint32 IndiciesCount() const
        {
            return static_cast<uint32>(mIndicies.GetSize() / sizeof(uint32));
        }

        inline uint32 GetSubMeshCount() const
        {
            return static_cast<uint32>(mSubMeshes.size());
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

    struct MipmapLayout
    {
        uint32 BaseOffset; // offset from the beginning of the texture
        uint32 MipSize; // size in bytes
        uint32 Width; // mipmap texture width 
        uint32 Height; // mipmap texture height
    };

    struct TextureInfo
    {
        uint16 Width;
        uint16 Height;
        uint16 Depth; // depth part is not implemented yet, just a placeholder
        uint16 MipLevels;
        ETextureFormat Format;
        uint8 _Padding[3];

        bool operator==(const TextureInfo& other) const = default;
    };

    uint32 CalculateTextureSize(uint32 width, uint32 height, uint32 mip_levels, uint32 pixel_size);
    MipmapLayout CalculateMipmapLayout(uint32 width, uint32 height, uint32 mip_levels, uint32 pixel_size, uint32 mip_slice);

    // holds pixels of a 2d texture mip chain
    class TextureData
    {
    public:
        TextureData()
            :mInfo{ .Width = 0, .Height = 0, .Depth = 0, .MipLevels = 0, .Format = ETextureFormat_None }
        {
        }

        explicit TextureData(uint16 height, uint16 width, uint16 mip_size, ETextureFormat format)
            :mInfo{ .Width = width, .Height = height, .Depth = 1, .MipLevels = mip_size, .Format = format }, mData(TextureSize())
        {
            ASSERT((height % 4) == 0 && (width % 4) == 0);
        }

        explicit TextureData(BinaryData binary_data, uint16 height, uint16 width, uint16 mip_size, ETextureFormat format)
            :
            mInfo{ .Width = width, .Height = height, .Depth = 1, .MipLevels = mip_size, .Format = format }, mData(std::move(binary_data))
        {
            // BC1 requires the width and height must be a multiple of 4
            ASSERT((height % 4) == 0 && (width % 4) == 0);

            // @binary_data size must match up with the mip chain size
            ASSERT(TextureSize() == mData.GetSize());
        }

        ~TextureData() = default;

        TextureData(const TextureData& other) = delete;
        TextureData(TextureData&& other);
        TextureData& operator=(TextureData other);

        inline uint16 Width() const { return mInfo.Width; }
        inline uint16 Height() const { return mInfo.Height; }
        inline uint16 MipLevels() const { return mInfo.MipLevels; }
        inline uint32 ChannelCount() const { return GetChannelCount(mInfo.Format); }
        inline ETextureFormat Format() const { return mInfo.Format; }
        inline const void* Data() const { return mData.GetData(); }
        inline uint32 DataSize() const { return mData.GetSize(); }
        inline bool Empty() const { return mData.Empty(); }

        uint32 PixelSize() const;
        uint32 TextureSize() const;

        // sample pixel on (u, v)
        Vector4 Sample(float u, float v) const;

        // set pixel on (u, v)
        void SetPixel(uint32 u, uint32 v, const Vector4& color);

        friend void Swap(TextureData& lhs, TextureData& rhs)
        {
            Swap(lhs.mData, rhs.mData);
            std::swap(lhs.mInfo, rhs.mInfo);
        }

    public:
        static void BinarySerialize(RingBuffer& rb, const TextureData& texture_data);
        static void BinaryDeserialize(RingBuffer& rb, TextureData& out_texture_data);

        // theta: angle between y-axis phi: angle between x-axis
        static Vector4 SampleTextureCube(const std::array<TextureData, 6>& data, float theta, float phi);

    public:
        TextureInfo mInfo;
        BinaryData mData;
    };
}