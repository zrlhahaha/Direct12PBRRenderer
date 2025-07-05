#include <d3d11.h>
#include "Resource/TextureCompression.h"

namespace MRenderer
{
    bool TextureCompressor::IsHDRFormat(DXGI_FORMAT format)
    {
        // anything between R32G32B32A32_TYPELESS and R32G32_SINT in DXGI_FORMAT is considered as HDR format
        return format >= ETextureFormat_R32G32B32A32_TYPELESS && format <= ETextureFormat_R32G32_SINT;
    }

    DXGI_FORMAT TextureCompressor::GetCompressedFormat(DXGI_FORMAT format)
    {
        if (IsHDRFormat(format))
        {
            return HDRTextureBCFormat;
        }
        else
        {
            return LDRTextureBCFormat;
        }
    }

    TextureCompressor::TextureCompressor()
    {
        UINT flags = 0;
        flags |= D3D11_CREATE_DEVICE_DEBUG;

        D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_10_0;

        //BC6H requires d3d11 device
        ThrowIfFailed(D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            flags,
            &feature_level,
            1,
            D3D11_SDK_VERSION,
            &mDevice,
            nullptr,
            &mContext
        ));
    }

    TextureCompressor* TextureCompressor::Instance()
    {
        static TextureCompressor instance;
        return &instance;
    }

    void TextureCompressor::Compress(uint32 width, uint32 height, uint32 mip_levels, ETextureFormat format, uint32 data_size, const uint8* data_ptr, CompressionHandler on_complete)
    {
        DXGI_FORMAT original_format = static_cast<DXGI_FORMAT>(format);
        DXGI_FORMAT compressed_format = GetCompressedFormat(static_cast<DXGI_FORMAT>(format));
        TextureCompressInternal(width, height, mip_levels, original_format, compressed_format, data_size, data_ptr, on_complete);
    }

    void TextureCompressor::Decompress(uint32 width, uint32 height, uint32 mip_levels, ETextureFormat format, uint32 data_size, const uint8* data_ptr, CompressionHandler on_complete)
    {
        DXGI_FORMAT original_format = static_cast<DXGI_FORMAT>(format);
        DXGI_FORMAT compressed_format = GetCompressedFormat(static_cast<DXGI_FORMAT>(format));
        TextureDecompressInternal(width, height, mip_levels, original_format, compressed_format, data_size, data_ptr, on_complete);
    }

    // ref: https://github.com/microsoft/DirectXTex/wiki/Compress
    // BC1 format goes to this function
    void TextureCompressor::TextureCompressInternal(uint32 width, uint32 height, uint32 mip_levels, DXGI_FORMAT orignal_format, DXGI_FORMAT compressed_format, uint32 data_size, const uint8* data_ptr, CompressionHandler on_complete)
    {
        // we use block compression to store the image file
        DirectX::ScratchImage raw_image;
        ThrowIfFailed(raw_image.Initialize2D(orignal_format, width, height, 1, mip_levels));

        uint32 mem_size = CalculateTextureSize(width, height, mip_levels, GetPixelSize(orignal_format));
        ASSERT(raw_image.GetPixelsSize() == mem_size);

        memcpy(raw_image.GetPixels(), data_ptr, mem_size);

        // do the compression
        DirectX::ScratchImage compressed;
        if (compressed_format != HDRTextureBCFormat) 
        {
            ThrowIfFailed(
                DirectX::Compress(
                    raw_image.GetImages(),
                    raw_image.GetImageCount(),
                    raw_image.GetMetadata(),
                    compressed_format,
                    DirectX::TEX_COMPRESS_DEFAULT,
                    DirectX::TEX_THRESHOLD_DEFAULT,
                    compressed
                )
            );
        }
        else 
        {
            // BC6H compression for HDR textures
            ThrowIfFailed(
                DirectX::Compress(
                    mDevice.Get(),
                    raw_image.GetImages(),
                    raw_image.GetImageCount(), 
                    raw_image.GetMetadata(),
                    compressed_format,
                    DirectX::TEX_COMPRESS_DEFAULT,
                    DirectX::TEX_ALPHA_WEIGHT_DEFAULT,
                    compressed
                )
            );
        }

        on_complete(static_cast<uint32>(compressed.GetPixelsSize()), compressed.GetPixels());
    }

    void TextureCompressor::TextureDecompressInternal(uint32 width, uint32 height, uint32 mip_levels, DXGI_FORMAT original_format, DXGI_FORMAT compressed_format, uint32 data_size, const uint8* data_ptr, CompressionHandler on_complete)
    {
        // basicly the inverse process of TextureCompressOnCPU
        DirectX::ScratchImage compressed;
        ThrowIfFailed(compressed.Initialize2D(compressed_format, width, height, 1, mip_levels));

        ASSERT(data_size == compressed.GetPixelsSize());
        memcpy(compressed.GetPixels(), data_ptr, data_size);

        DirectX::ScratchImage image;
        ThrowIfFailed(
            DirectX::Decompress(
                compressed.GetImages(),
                compressed.GetImageCount(),
                compressed.GetMetadata(),
                original_format,
                image
            )
        );

        uint32 mem_size = CalculateTextureSize(width, height, mip_levels, GetPixelSize(original_format));
        ASSERT(image.GetPixelsSize() == mem_size);

        on_complete(static_cast<uint32>(image.GetPixelsSize()), image.GetPixels());
    }
}