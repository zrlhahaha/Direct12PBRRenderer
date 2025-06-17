#include <d3d11.h>
#include "Resource/TextureCompression.h"

namespace MRenderer
{
    bool TextureCompressor::IsHDRFormat(DXGI_FORMAT format)
    {
        // anything between R32G32B32A32_TYPELESS and R32G32_SINT in DXGI_FORMAT is considered as HDR format
        return format >= ETextureFormat_R32G32B32A32_TYPELESS && format <= ETextureFormat_R32G32_SINT;
    }

    uint32 TextureCompressor::TextureMemorySize(uint32 width, uint32 height, DXGI_FORMAT format)
    {
        return static_cast<uint32>(width * height * DirectX::BitsPerPixel(format) / CHAR_BIT);
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

    void TextureCompressor::Compress(const uint8* data, uint32 width, uint32 height, ETextureFormat format, CompressionHandler on_complete)
    {
        DXGI_FORMAT original_format = static_cast<DXGI_FORMAT>(format);
        DXGI_FORMAT compressed_format = GetCompressedFormat(static_cast<DXGI_FORMAT>(format));
        TextureCompressInternal(data, width, height, original_format, compressed_format, on_complete);
    }

    void TextureCompressor::Decompress(const uint8* data, uint32 width, uint32 height, ETextureFormat format, CompressionHandler on_complete)
    {
        DXGI_FORMAT original_format = static_cast<DXGI_FORMAT>(format);
        DXGI_FORMAT compressed_format = GetCompressedFormat(static_cast<DXGI_FORMAT>(format));
        TextureDecompressInternal(data, width, height, original_format, compressed_format, on_complete);
    }

    // ref: https://github.com/microsoft/DirectXTex/wiki/Compress
    // BC1 format goes to this function
    void TextureCompressor::TextureCompressInternal(const uint8* data, uint32 width, uint32 height, DXGI_FORMAT orignal_format, DXGI_FORMAT compressed_format, CompressionHandler on_complete)
    {
        // we use block compression to store the image file
        DirectX::ScratchImage raw_image;
        ThrowIfFailed(raw_image.Initialize2D(orignal_format, width, height, 1, 1));
        memcpy(raw_image.GetPixels(), data, TextureMemorySize(width, height, orignal_format));

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

    void TextureCompressor::TextureDecompressInternal(const uint8* data, uint32 width, uint32 height, DXGI_FORMAT original_format, DXGI_FORMAT compressed_format, CompressionHandler on_complete)
    {
        // basicly the inverse process of TextureCompressOnCPU
        DirectX::ScratchImage compressed;
        ThrowIfFailed(compressed.Initialize2D(compressed_format, width, height, 1,1));

        memcpy(compressed.GetPixels(), data, TextureMemorySize(width, height, compressed_format));

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

        on_complete(static_cast<uint32>(image.GetPixelsSize()), image.GetPixels());
    }
}