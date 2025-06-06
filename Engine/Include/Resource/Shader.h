#pragma once
#include <vector>
#include <filesystem>
#include <d3d12shader.h>
#include <unordered_map>

#include "dxcapi.h"
#include "Fundation.h"
#include "Renderer/Device/Direct12/D3DUtils.h"

namespace MRenderer
{
    enum EShaderType
    {
        EShaderType_Vertex,
        EShaderType_Pixel,
        EShaderType_Compute,
        EShaderType_Total,
    };

    enum EShaderAttrType 
    {
        EShaderAttrType_None = 0,
        EShaderAttrType_Texture,
        EShaderAttrType_Sampler,
        EShaderAttrType_RWTexture,
        EShaderAttrType_ConstantBuffer,
        EShaderAttrType_StructuredBuffer,
        EShaderAttrType_RWStructuredBuffer,
    };


    struct ShaderAttribute
    {
    public:
        ShaderAttribute(EShaderAttrType type, uint16 bind_point, uint16 bind_count, std::string_view name)
            : mType(type), mBindPoint(bind_point), mBindCount(bind_count), mName(name)
        {
        }

        EShaderAttrType mType;
        uint16 mBindPoint;
        uint16 mBindCount;
        
        std::string mName;
    };


    struct ShaderConstantBufferVarriable
    {
    public:
        ShaderConstantBufferVarriable(ID3D12ShaderReflectionVariable* reflection)
        {
            D3D12_SHADER_VARIABLE_DESC desc;
            ThrowIfFailed(reflection->GetDesc(&desc));

            mName = desc.Name;
            mSize = desc.Size;
            mOffset = desc.StartOffset;
        }

    public:
        std::string mName;
        uint16 mSize;
        uint16 mOffset;
    };


    struct ShaderConstantBufferAttribute : public ShaderAttribute
    {
        ShaderConstantBufferAttribute(uint16 bind_point, uint16 bind_count, std::string_view name, ID3D12ShaderReflectionConstantBuffer* reflection)
            :ShaderAttribute(EShaderAttrType_ConstantBuffer, bind_point, bind_count, name)
        {
            D3D12_SHADER_BUFFER_DESC desc;
            reflection->GetDesc(&desc);

            mName = desc.Name;
            mVaraibleCount = desc.Variables;
            mSize = desc.Size;

            // retrieve all attributes in the constant buffer
            mAttributes.reserve(desc.Variables);
            for (uint32 i = 0; i < desc.Variables; i++)
            {
                ID3D12ShaderReflectionVariable* var_reflection = reflection->GetVariableByIndex(i);
                mAttributes.emplace_back(var_reflection);
            }
        }

    public:
        std::string mName;
        uint32 mVaraibleCount;
        uint32 mSize;
        std::vector<ShaderConstantBufferVarriable> mAttributes;
    };

    class D3D12ShaderCompilation
    {
    public:
        D3D12ShaderCompilation(IDxcBlob* code_blob, ID3D12ShaderReflection* shader_reflection);

        inline uint32 GetTextureCount() const
        {
            return CountAttribute(EShaderAttrType_Texture);
        }

        inline const ShaderAttribute* GetTextureAttribute(uint32 index) const 
        {
            return IndexAttribute(EShaderAttrType_Texture, index);
        }

        inline uint32 GetConstantBufferCount() const
        {
            return static_cast<uint32>(mConstantBuffer.size());
        }

        inline const ShaderConstantBufferAttribute* GetConstantBufferAttribute(uint32 index) const
        {
            return &mConstantBuffer[index];
        }

        const ShaderAttribute* FindAttribute(EShaderAttrType attr_type, std::string_view semantic_name) const;

        const ShaderConstantBufferAttribute* FindConstantBufferAttribute(std::string sematics_name) const;

        inline IDxcBlob* GetShaderByteCode() const
        {
            return mCodeBlob.Get();
        }

    protected:

        // count how many attributes of the type in the shader
        inline uint32 CountAttribute(EShaderAttrType type) const;

        // find the index-th attribute of the type in the shader
        inline const ShaderAttribute* IndexAttribute(EShaderAttrType type, uint32 index) const;

    protected:
        ComPtr<IDxcBlob> mCodeBlob;
        ComPtr<ID3D12ShaderReflection> mShaderReflection;
        D3D12_SHADER_DESC mShaderDesc;

        std::vector<ShaderConstantBufferAttribute> mConstantBuffer;
        std::vector<ShaderAttribute> mShaderAttribute; // texture, uav, rwstructured buffer
    };


    class D3D12ShaderCompiler
    {
    public:
        // shader code file should use utf8 encoding
        static constexpr uint32 CodePage = CP_UTF8;

    public:
        D3D12ShaderCompiler()
        {
            ThrowIfFailed(DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&mLibrary)));
            ThrowIfFailed(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&mCompiler)));
            ThrowIfFailed(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&mUtils)));

            ThrowIfFailed(mUtils->CreateDefaultIncludeHandler(&mDefaultIncludeHandler));
        }

        std::unique_ptr<D3D12ShaderCompilation> Compile(std::string_view path, EShaderType type);

    public:
        static constexpr std::wstring ShaderTypeString(EShaderType type)
        {
            const wchar_t* type_string[] =
            {
                L"vs",
                L"ps",
                L"cs"
            };

            return type_string[type];
        }

        static constexpr std::wstring ShaderProfile(EShaderType type)
        {
            // shader model is fixed to 6.0
            return ShaderTypeString(type) + L"_6_0";
        }

        static constexpr std::wstring ShaderEntryPoint(EShaderType type)
        {
            // shader entry point is fixed to e.g vs_main
            return ShaderTypeString(type) + L"_main";
        }

    protected:
        ComPtr<IDxcLibrary> mLibrary;
        ComPtr<IDxcCompiler3> mCompiler;
        ComPtr<IDxcUtils> mUtils;
        ComPtr<IDxcIncludeHandler> mDefaultIncludeHandler;
    };

    class D3D12ShaderProgram
    {
    private:
        inline static uint8 ShaderCode = 0;

    public:
        D3D12ShaderProgram(std::string_view file_path, std::unique_ptr<D3D12ShaderCompilation> vs_shader, std::unique_ptr<D3D12ShaderCompilation> ps_shader, std::unique_ptr<D3D12ShaderCompilation> cs_shader)
            :mVS(std::move(vs_shader)), mPS(std::move(ps_shader)), mCS(std::move(cs_shader)), mFilePath(file_path), mHashCode(ShaderCode++)
        {
        }

        // each shader may have a cbuffer named  Shader, which is for shader relatived parameters
        uint32 GetConstantBufferSize(EShaderType type) const
        {
            const std::unique_ptr<D3D12ShaderCompilation> D3D12ShaderProgram::* mapping[] = 
            { 
                &D3D12ShaderProgram::mVS,
                &D3D12ShaderProgram::mPS, 
                &D3D12ShaderProgram::mCS 
            };

            auto& shader = this->*mapping[type];
            ASSERT(shader);

            const ShaderConstantBufferAttribute* attr = shader->FindConstantBufferAttribute("Shader");
            return attr ? attr->mSize : 0;
        }

        uint32 GetTextureBindPointEdge() const 
        {
            uint32 max_bind_point = 0;
            for (uint32 i = 0; i < mVS->GetTextureCount(); i++) 
            {
                max_bind_point = max(max_bind_point, mVS->GetTextureAttribute(i)->mBindPoint);
            }

            return max_bind_point;
        }

        std::string_view GetFilePath() const
        {
            return mFilePath;
        }

    public:
        std::unique_ptr<D3D12ShaderCompilation> mVS;
        std::unique_ptr<D3D12ShaderCompilation> mPS;
        std::unique_ptr<D3D12ShaderCompilation> mCS;
        std::string mFilePath;
        uint8 mHashCode;
    };


    class ShaderLibrary
    {
    public:
        D3D12ShaderProgram* ComplieShader(std::string_view name, bool is_compute)
        {
            const auto& it = mCache.find(name.data());
            if (it != mCache.end()) 
            {
                return it->second.get();
            }

            using std::filesystem::path;
            path file_path = path(ShaderFolderPath) / path(name);

            ASSERT(std::filesystem::exists(file_path));

            if (!is_compute) 
            {
                std::unique_ptr<D3D12ShaderCompilation> vs = mCompiler.Compile(file_path.string(), EShaderType_Vertex);
                std::unique_ptr<D3D12ShaderCompilation> ps = mCompiler.Compile(file_path.string(), EShaderType_Pixel);
                ASSERT(vs && ps);

                mCache[name.data()] = std::make_unique<D3D12ShaderProgram>(name, std::move(vs), std::move(ps), nullptr);
            }
            else 
            {
                std::unique_ptr<D3D12ShaderCompilation> cs = mCompiler.Compile(file_path.string(), EShaderType_Compute);
                ASSERT(cs);

                mCache[name.data()] = std::make_unique<D3D12ShaderProgram>(name, nullptr, nullptr, std::move(cs));
            }

            return mCache[name.data()].get();
        }

        static ShaderLibrary& Instance()
        {
            static ShaderLibrary lib;
            return lib;
        }
    protected:
        std::unordered_map<std::string, std::unique_ptr<D3D12ShaderProgram>> mCache;
        D3D12ShaderCompiler mCompiler;
    };
}