#include <d3d11.h>
#include "Resource/ResourceDef.h"
#include "DirectXTex.h"

namespace MRenderer
{
    class TextureCompressor
    {
    public:
        using CompressionHandler = std::function<void(uint32, const uint8*)>; // func(uint32 data_size, const uint8* data_pointer)
        using DecompressionHandler = std::function<void(uint32, const uint8*)>; // func(uint32 data_size, const uint8* data_pointer)

        static const DXGI_FORMAT LDRTextureBCFormat = DXGI_FORMAT_BC1_UNORM;
        static const DXGI_FORMAT HDRTextureBCFormat = DXGI_FORMAT_BC6H_UF16;

    public:
        static TextureCompressor* Instance();

        void Compress(uint32 width, uint32 height, uint32 mip_levels, ETextureFormat format, uint32 data_size, const uint8* data_ptr, CompressionHandler on_complete);
        void Decompress(uint32 width, uint32 height, uint32 mip_levels, ETextureFormat format, uint32 data_size, const uint8* data_ptr, CompressionHandler on_complete);

    protected:
        TextureCompressor();

        void TextureCompressInternal(uint32 width, uint32 height, uint32 mip_levels, DXGI_FORMAT orignal_format, DXGI_FORMAT compressed_format, uint32 data_size, const uint8* data_ptr, CompressionHandler on_complete);
        void TextureDecompressInternal(uint32 width, uint32 height, uint32 mip_levels, DXGI_FORMAT original_format, DXGI_FORMAT compressed_format, uint32 data_size, const uint8* data_ptr, CompressionHandler on_complete);

        static DXGI_FORMAT GetCompressedFormat(DXGI_FORMAT format);
        static bool IsHDRFormat(DXGI_FORMAT format);
    protected:
        // directxtex BC6H compress don't support D3D12, so we need to create d3d11 device here for gpu BC6H compression
        ComPtr<ID3D11Device> mDevice;
        ComPtr<ID3D11DeviceContext> mContext;
    };
}