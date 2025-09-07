#include "Resource/BasicStorage.h"
#include "Resource/TextureCompression.h"
#include "Utils/Serialization.h"

namespace MRenderer
{
    uint32 GetChannelCount(ETextureFormat format)
    {
        switch (format)
        {
        case ETextureFormat_R16G16B16A16_UNORM:
        case ETextureFormat_R8G8B8A8_UNORM:
        case ETextureFormat_R32G32B32A32_FLOAT:
        case ETextureFormat_R16G16B16A16_FLOAT:
            return 4;
        case ETextureFormat_R8G8_UNORM:
        case ETextureFormat_R16G16_UNORM:
        case ETextureFormat_R32G32_SINT:
            return 2;
        case ETextureFormat_R8_UNORM:
            return 1;
        default:
            ASSERT(false);
            return 0;
        }
        return 0;
    }

    uint32 GetPixelSize(ETextureFormat format)
    {
        return GetPixelSize(static_cast<DXGI_FORMAT>(format));
    }

    uint32 GetPixelSize(DXGI_FORMAT format)
    {
        return static_cast<uint32>(DirectX::BitsPerPixel(format) / CHAR_BIT);
    }

    BinaryData::BinaryData()
        :mSize(0), mData(nullptr)
    {
    }

    BinaryData::BinaryData(uint32 size)
    {
        mSize = size;
        mData = reinterpret_cast<uint8*>(malloc(size));
    }

    BinaryData::BinaryData(const void* src_ptr, uint32 size)
        :BinaryData(size)
    {
        memcpy(mData, src_ptr, size);
    }

    BinaryData::~BinaryData()
    {
        if (mData != nullptr)
        {
            free(mData);
            mData = nullptr;
            mSize = 0;
        }
    }

    BinaryData::BinaryData(BinaryData&& other)
        :BinaryData()
    {
        swap(*this, other);
    }

    BinaryData& BinaryData::operator=(BinaryData other)
    {
        swap(*this, other);
        return *this;
    }

    void BinaryData::BinarySerialize(RingBuffer& rb, const BinaryData& binary)
    {
        rb.Write(binary.mSize);
        rb.Write(reinterpret_cast<const uint8*>(binary.mData), binary.mSize);
    }

    void BinaryData::BinaryDeserialize(RingBuffer& rb, BinaryData& out)
    {
        uint32 size = rb.Read<uint32>();
        const void* buffer = rb.Read(size);

        out = BinaryData(buffer, size);
    }

    MeshData::MeshData(MeshData&& rhs)
        :MeshData()
    {
        swap(*this, rhs);
    }

    MeshData& MeshData::operator=(MeshData rhs)
    {
        swap(*this, rhs);
        return *this;
    }

    TextureData::TextureData(TextureData&& other)
        :TextureData()
    {
        swap(*this, other);
    }

    TextureData& TextureData::operator=(TextureData other)
    {
        swap(*this, other);
        return *this;
    }

    uint32 TextureData::PixelSize() const
    {
        return static_cast<uint32>(DirectX::BitsPerPixel(static_cast<DXGI_FORMAT>(mInfo.Format)) / CHAR_BIT);
    }

    uint32 TextureData::TextureSize() const
    {
        return CalculateTextureSize(mInfo.Width, mInfo.Height, mInfo.MipLevels, PixelSize());
    }

    Vector4 TextureData::Sample(float u, float v) const
    {
        // warn: only support DXGI_FORMAT_R32G32B32A32_FLOAT for now
        ASSERT(mInfo.Format == ETextureFormat_R32G32B32A32_FLOAT);

        uint32 row = std::clamp<uint32>(static_cast<uint32>(u * mInfo.Width), 0, mInfo.Width - 1);
        uint32 col = std::clamp<uint32>(static_cast<uint32>(v * mInfo.Height), 0, mInfo.Height - 1);
        uint32 tex_index = col * mInfo.Width + row;
        const float* pixel = reinterpret_cast<const float*>(mData.Offset(tex_index * PixelSize()));

        Vector4 ret;
        for (uint32 i = 0; i < ChannelCount(); i++)
        {
            ret[i] = pixel[i];
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

    void TextureData::BinarySerialize(RingBuffer& rb, const TextureData& texture_data)
    {
        TextureCompressor::Instance()->Compress(texture_data.mInfo.Width, texture_data.mInfo.Height, texture_data.MipLevels(), texture_data.mInfo.Format, texture_data.mData.GetSize(), static_cast<const uint8*>(texture_data.mData.GetData()),
            [&](uint32 size, const uint8* data)
            {
                BinarySerialization::Serialize(rb, texture_data.mInfo);
                rb.Write<uint32>(size);
                rb.Write(data, size);
            }
        );
    }

    void TextureData::BinaryDeserialize(RingBuffer& rb, TextureData& out_texture_data)
    {
        TimeScope _scrop("TextureData::BinaryDeserialize");

        BinarySerialization::Deserialize(rb, out_texture_data.mInfo);

        uint32 compressed_size = rb.Read<uint32>();
        const uint8* pixels = rb.Read(compressed_size);

        TextureCompressor::Instance()->Decompress(out_texture_data.mInfo.Width, out_texture_data.mInfo.Height, out_texture_data.mInfo.MipLevels, out_texture_data.mInfo.Format, compressed_size, pixels,
            [&](uint32 size, const uint8* data)
            {
                out_texture_data.mData = BinaryData(data, size);
            }
        );
    }

    // theta: angle between y-axis phi: angle between x-axis
    Vector4 CubeMapTextureData::Sample(const std::array<TextureData, 6>& data, float theta, float phi)
    {
        Vector2 tc;
        uint32 index = 0;
        CalcCubeMapCoordinate(FromSphericalCoordinate(theta, phi), index, tc);

        const TextureData& slice = data[index];
        return slice.Sample(tc.x, tc.y);
    }

    SH2CoefficientsPack CubeMapTextureData::GenerateSHCoefficients(const std::array<TextureData, NumCubeMapFaces>& texture)
    {
        SH2Coefficients shr;
        SH2Coefficients shg;
        SH2Coefficients shb;

        SHBaker::ProjectEnvironmentMap(texture, shr, shg, shb);
        return SHBaker::PackCubeMapSHCoefficient(shr, shg, shb);
    }
}