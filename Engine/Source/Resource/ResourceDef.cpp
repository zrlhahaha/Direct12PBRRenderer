#include "Resource/ResourceDef.h"
#include "DirectXTex.h"
#include "Utils/Serialization.h"
#include "Resource/Shader.h"
#include "Resource/ResourceLoader.h"
#include "Renderer/Device/Direct12/D3D12Device.h"
#include "Resource/json.hpp"
#include "format"

namespace MRenderer
{
    BinaryData::BinaryData()
        :mSize(0), mData(nullptr)
    {
    }

    BinaryData::BinaryData(uint32 size)
    {
        mSize = size;
        mData = malloc(size);
    }

    BinaryData::BinaryData(const void* src_ptr, uint32 size)
        :mSize(size)
    {
        mData = malloc(size);
        memcpy(mData, src_ptr, size);
    }

    BinaryData::~BinaryData()
    {
        if (mData == nullptr)
        {
            free(mData);
            mData = nullptr;
            mSize = 0;
        }
    }

    BinaryData::BinaryData(BinaryData&& other)
        :BinaryData()
    {
        Swap(*this, other);
    }

    BinaryData& BinaryData::operator=(BinaryData other)
    {
        Swap(*this, other);
        return *this;
    }

    void BinaryData::Serialize(RingBuffer& rb, const BinaryData& binary)
    {
        rb.Write(binary.mSize);
        rb.Write(reinterpret_cast<const unsigned char*>(binary.mData), binary.mSize);
    }

    void BinaryData::Deserialize(RingBuffer& rb, BinaryData& out)
    {
        uint32 size = rb.Read<uint32>();
        const void* buffer = rb.Read(size);

        out = BinaryData(buffer, size);
    }

    MeshData::MeshData(MeshData&& rhs)
        :MeshData()
    {
        Swap(*this, rhs);
    }

    MeshData& MeshData::operator=(MeshData rhs)
    {
        Swap(*this, rhs);
        return *this;
    }

    TextureData::TextureData(TextureData&& other)
        :TextureData()
    {
        Swap(*this, other);
    }

    TextureData& TextureData::operator=(TextureData other)
    {
        Swap(*this, other);
        return *this;
    }

    uint32 TextureData::PixelSize() const
    {
        return DirectX::BitsPerPixel(static_cast<DXGI_FORMAT>(mInfo.Format)) / CHAR_BIT;
    }

    Vector4 TextureData::Sample(float u, float v) const
    {
        // warn: only support r8g8b8a8 for now
        ASSERT(mInfo.Format == ETextureFormat_R8G8B8A8_UNORM);

        uint32 row =  std::clamp<uint32>(static_cast<uint32>(u * mInfo.Width), 0, mInfo.Width - 1);
        uint32 col =  std::clamp<uint32>(static_cast<uint32>(v * mInfo.Height), 0, mInfo.Height - 1);
        uint32 tex_index = col * mInfo.Width + row;
        uint8* pixel = reinterpret_cast<uint8*>(mData.GetData()) + tex_index * PixelSize();

        Vector4 ret;
        for (uint32 i = 0; i < ChannelCount(); i++)
        {
            ret[i] = pixel[i] * Inv255;
        }
        return ret;
    }

    void TextureData::SetPixel(uint32 x, uint32 y, const Vector4& color)
    {
        // warn: only support r8g8b8a8 for now
        ASSERT(mInfo.Format == ETextureFormat_R8G8B8A8_UNORM);

        uint32 row = std::clamp<uint32>(x, 0, mInfo.Width - 1);
        uint32 col = std::clamp<uint32>(y, 0, mInfo.Height - 1);
        uint32 tex_index = col * mInfo.Width + row;
        uint8* pixel = reinterpret_cast<uint8*>(mData.GetData()) + tex_index * PixelSize();

        for (uint32 i = 0; i < ChannelCount(); i++) 
        {
            uint8 channel = std::clamp<uint8>(static_cast<uint8>(color[i] * 255), 0, 255);
            pixel[i] = channel;
        }
    }

    void TextureData::Serialize(RingBuffer& rb, const TextureData& texture_data)
    {
        // compressed as BC 1
        DirectX::ScratchImage raw_image;
        ThrowIfFailed(
            raw_image.Initialize2D(static_cast<DXGI_FORMAT>(texture_data.mInfo.Format),
                texture_data.mInfo.Width,
                texture_data.mInfo.Height,
                1,
                1
            )
        );
        
        const DirectX::Image* image_slice = raw_image.GetImage(0, 0, 0);
        memcpy(image_slice->pixels, texture_data.mData.GetData(), texture_data.mData.GetSize());

        DirectX::ScratchImage compressed;
        ThrowIfFailed(
            DirectX::Compress(
                raw_image.GetImages(),
                raw_image.GetImageCount(),
                raw_image.GetMetadata(),
                DXGI_FORMAT_BC1_UNORM,
                DirectX::TEX_COMPRESS_DEFAULT,
                1.0f,
                compressed
            )
        );

        BinarySerialization::Serialize(rb, texture_data.mInfo);
        rb.Write<uint32>(compressed.GetPixelsSize());
        rb.Write(compressed.GetPixels(), compressed.GetPixelsSize());
    }

    void TextureData::Deserialize(RingBuffer& rb, TextureData& out_texture_data)
    {
        BinarySerialization::Deserialize(rb, out_texture_data.mInfo);
        uint32 buffer_size = rb.Read<uint32>();
        const void* buffer = rb.Read(buffer_size);

        DirectX::ScratchImage compressed;
        ThrowIfFailed(
            compressed.Initialize2D(
                DXGI_FORMAT_BC1_UNORM,
                out_texture_data.mInfo.Width,
                out_texture_data.mInfo.Height,
                1,
                1
            )
        );

        memcpy(compressed.GetPixels(), buffer, buffer_size);

        DirectX::ScratchImage raw_image;
        ThrowIfFailed(
            DirectX::Decompress(
                compressed.GetImages(),
                compressed.GetImageCount(),
                compressed.GetMetadata(),
                static_cast<DXGI_FORMAT>(out_texture_data.mInfo.Format),
                raw_image
            )
        );

        const DirectX::Image* image_slice = raw_image.GetImage(0, 0, 0);
        out_texture_data.mData = BinaryData(image_slice->pixels, image_slice->slicePitch);
    }

    // theta: angle between y-axis phi: angle between x-axis

    Vector4 TextureData::SampleTextureCube(const std::array<TextureData, 6>& data, float theta, float phi)
    {
        Vector2 tc;
        uint32 index = 0;
        CalcCubeMapCoordinate(FromSphericalCoordinate(theta, phi), index, tc);

        const TextureData& slice = data[index];
        return slice.Sample(tc.x, tc.y);
    }

    // theta: angle between y-axis phi: angle between x-axis

    void MeshResource::AllocateGPUResource()
    {
        // assume there won't be any reallocation for now
        ASSERT(!mDeviceVertexBuffer);
        ASSERT(!mDeviceIndexBuffer);

        VertexDefination vertex = GetVertexLayout(mMeshData.mVertexFormat);

        mDeviceVertexBuffer = GD3D12Device->CreateVertexBuffer(
            mMeshData.mVertices.GetData(),
            mMeshData.mVertices.GetSize(),
            vertex.VertexSize
        );

        mDeviceIndexBuffer = GD3D12Device->CreateIndexBuffer(
            static_cast<uint32*>(mMeshData.mIndicies.GetData()),
            mMeshData.mIndicies.GetSize()
        );
    }


    MeshResource::MeshResource(std::string_view repo_path, MeshData mesh_data)
        :IResource(), mMeshData(std::move(mesh_data))
    {
        SetRepoPath(repo_path);
        AllocateGPUResource();
    }

    void MeshResource::PostDeserialized()
    {
        AllocateGPUResource();
    }

    void TextureResource::PostDeserialized()
    {
        AllocateGPUResource();
    }

    void TextureResource::AllocateGPUResource()
    {
        mDeviceTexture = GD3D12Device->CreateTexture2D(
            mTextureData.mInfo.Width,
            mTextureData.mInfo.Height,
            mTextureData.mInfo.Format,
            mTextureData.mData.GetData()
        );
    }

    void MaterialResource::PostSerialized() const
    {
        for (auto& tex : mTextureRefs)
        {
            ResourceLoader::Instance().DumpResource(*tex);
        }
    }
    
    void MaterialResource::PostDeserialized()
    {
        if (!mShaderPath.empty())
        {
            SetShader(mShaderPath);
        }

        for (auto& it : mTexturePath)
        {
            SetTexture(it.first, it.second);
        }
    }

    void MaterialResource::SetShader(std::string filename)
    {
        mShaderPath = filename;
        mShadingState->SetShader(filename, false);
    }

    void MaterialResource::SetShaderParameter(std::string name, ShaderParameter val)
    {
        mParameterTable[name] = val;
    }

    void MaterialResource::SetTexture(std::string_view semantic_name, std::string_view repo_path)
    {
        std::shared_ptr<TextureResource> tex = ResourceLoader::Instance().LoadResource<TextureResource>(repo_path);
        if (!tex)
        {
            Log(std::format("Load TextureResource Failed From: {}", repo_path));
            return;
        }

        if (mShadingState->SetTexture(semantic_name, tex->Resource())) 
        {
            SetTexture(semantic_name, tex);
        }
    }

    void MaterialResource::SetTexture(std::string_view semantic_name, const std::shared_ptr<TextureResource>& texture_resource)
    {
        std::string_view texture_path = texture_resource->GetRepoPath();
        ASSERT(!texture_path.empty());
        mTexturePath[semantic_name.data()] = texture_path;
        mTextureRefs.push_back(texture_resource);

        if (!mShadingState->SetTexture(semantic_name, texture_resource->Resource()))
        {
            Log("Tring To Assigning Undefined Texture ", semantic_name, "To Material With Shader", mShadingState->GetShader()->GetFilePath());
        }
    }

    void ModelResource::PostSerialized() const
    {
        ResourceLoader::Instance().DumpResource(*mMeshResource);

        for (uint32 i = 0; i < mMaterials.size(); i++)
        {
            ResourceLoader::Instance().DumpResource(*mMaterials[i]);
        }
    }

    void ModelResource::PostDeserialized()
    {
        if (!mMeshPath.empty())
        {
            mMeshResource = ResourceLoader::Instance().LoadResource<MeshResource>(mMeshPath);
        }

        for (auto& path : mMaterialPath)
        {
            mMaterials.push_back(ResourceLoader::Instance().LoadResource<MaterialResource>(path));
        }
    }

    void ModelResource::SetMaterial(uint32 index, const std::shared_ptr<MaterialResource>& res)
    {
        const uint32 MaxMaterialCount = 8;
        ASSERT(index < MaxMaterialCount);

        uint32 minimum_size = index + 1;
        if (mMaterialPath.size() < (minimum_size))
        {
            mMaterialPath.resize(minimum_size);
            mMaterials.resize(minimum_size);
        }

        mMaterialPath[index] = res->GetRepoPath();
        mMaterials[index] = res;
    }

    void ModelResource::SetMesh(const std::shared_ptr<MeshResource>& res)
    {
        mMeshPath = res->GetRepoPath();
        mMeshResource = res;
    }

    void CubeMapResource::GenerateSHCoefficients()
    {
        SH2Coefficients shr;
        SH2Coefficients shg;
        SH2Coefficients shb;

        SHBaker::ProjectEnvironmentMap(mTextureData, shr, shg, shb);
        mSHCoefficients = SHBaker::PackCubeMapSHCoefficient(shr, shg, shb);
    }

    void CubeMapResource::PostDeserialized()
    {
        AllocateGPUResource();
    }

    void CubeMapResource::AllocateGPUResource()
    {
        mDeviceTexture2DArray = GD3D12Device->CreateTextureCube(mTextureData[0].mInfo.Width, mTextureData[0].mInfo.Height, 1, mTextureData[0].mInfo.Format, &mTextureData);
        mDeviceTexture2DArray->Resource()->SetName(L"CubeMap");
    }
}