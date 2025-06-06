#include "Resource/Shader.h"
#include "Utils/Misc.h"

namespace MRenderer
{
    std::unique_ptr<D3D12ShaderCompilation> MRenderer::D3D12ShaderCompiler::Compile(std::string_view path, EShaderType type)
    {
        uint32_t code_page = CodePage;
        std::wstring ws_path = ToWString(path);

        // compile shader file
        ComPtr<IDxcBlobEncoding> shader_blob;
        HRESULT hr = mLibrary->CreateBlobFromFile(ws_path.c_str(), &code_page, &shader_blob);
        if (hr != S_OK)
        {
            Warn("Load Shader Failed At", path);
            ThrowIfFailed(hr);
        }

        DxcBuffer shader_buffer;
        shader_buffer.Ptr = shader_blob->GetBufferPointer();
        shader_buffer.Size = shader_blob->GetBufferSize();
        shader_buffer.Encoding = CodePage;

        std::wstring&& shader_profile = ShaderProfile(type);
        std::wstring&& entry_point = ShaderEntryPoint(type);
        std::wstring file_name = std::filesystem::path(path).filename();

        // details in: https://simoncoenen.com/blog/programming/graphics/DxcCompiling
        const wchar_t* compile_arguments[] =
        {
            L"-E",
            entry_point.data(),
            L"-T",
            shader_profile.data(),
            DXC_ARG_PACK_MATRIX_ROW_MAJOR,
            DXC_ARG_WARNINGS_ARE_ERRORS,
            DXC_ARG_ALL_RESOURCES_BOUND,
            DXC_ARG_DEBUG, // disable optimization for now
            DXC_ARG_SKIP_OPTIMIZATIONS,
            L"-I",
            LShaderFolderPath,
            //L"-Qstrip_debug",
            L"-Qembed_debug"
        };

        ComPtr<IDxcResult> result;
        ThrowIfFailed(mCompiler->Compile(
            &shader_buffer, // pSource 0
            compile_arguments, // pSourceName
            static_cast<uint32>(std::size(compile_arguments)), // pEntryPoint
            mDefaultIncludeHandler.Get(),
            IID_PPV_ARGS(&result)
        ));

        ThrowIfFailed(result->GetStatus(&hr));

        ComPtr<IDxcBlobUtf8> error_blob;
        ThrowIfFailed(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&error_blob), nullptr));
        if (error_blob && error_blob->GetStringLength() > 0)
        {
            std::cout << error_blob->GetStringPointer() << std::endl;
        }

        // log error message if shader compilation failed
        if (FAILED(hr))
        {
            std::cout << "Compile Shader " << path << " Failed" << std::endl;
        }
        ThrowIfFailed(hr);

        ComPtr<IDxcBlob> pdb_blob;
        ComPtr<IDxcBlobUtf16> name_hint;
        result->GetOutput(DXC_OUT_PDB, IID_PPV_ARGS(&pdb_blob), &name_hint);

        //std::wstring pdb_path = std::wstring(L"Shader/") + name_hint->GetStringPointer();
        //std::ofstream pdb_file(pdb_path.data(), std::ios::binary);
        //pdb_file.write(static_cast<const char*>(pdb_blob->GetBufferPointer()), pdb_blob->GetBufferSize());

        // retrieve shader reflection infomation
        ComPtr<IDxcBlob> reflection_blob;
        ThrowIfFailed(result->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(&reflection_blob), nullptr));

        DxcBuffer reflection_buffer;
        reflection_buffer.Ptr = reflection_blob->GetBufferPointer();
        reflection_buffer.Size = reflection_blob->GetBufferSize();
        reflection_buffer.Encoding = 0;

        ID3D12ShaderReflection* shader_reflection;
        ThrowIfFailed(mUtils->CreateReflection(&reflection_buffer, IID_PPV_ARGS(&shader_reflection)));

        IDxcBlob* blob;
        ThrowIfFailed(result->GetResult(&blob));

        return std::make_unique<D3D12ShaderCompilation>(blob, shader_reflection);
    }

    D3D12ShaderCompilation::D3D12ShaderCompilation(IDxcBlob* code_blob, ID3D12ShaderReflection* shader_reflection)
        :mCodeBlob(code_blob), mShaderReflection(shader_reflection)
    {
        mShaderReflection->GetDesc(&mShaderDesc);

        for (uint32 i = 0, cbuffer_index = 0; i < mShaderDesc.BoundResources; i++)
        {
            D3D12_SHADER_INPUT_BIND_DESC desc;
            mShaderReflection->GetResourceBindingDesc(i, &desc);

            if (desc.Type == D3D_SIT_CBUFFER)
            {
                ID3D12ShaderReflectionConstantBuffer* cbuffer_reflection = shader_reflection->GetConstantBufferByIndex(cbuffer_index++);
                mConstantBuffer.emplace_back(desc.BindPoint, desc.BindCount, desc.Name, cbuffer_reflection);
            }
            else
            {
                EShaderAttrType attr_type = EShaderAttrType_None;
                if (desc.Type == D3D_SIT_TEXTURE)
                {
                    attr_type = EShaderAttrType_Texture;
                }
                else if (desc.Type == D3D_SIT_SAMPLER)
                {
                    attr_type = EShaderAttrType_Sampler;
                }
                else if (desc.Type == D3D11_SIT_UAV_RWTYPED) 
                {
                    attr_type = EShaderAttrType_RWTexture;
                }
                else if (desc.Type == D3D_SIT_STRUCTURED)
                {
                    attr_type = EShaderAttrType_StructuredBuffer;
                }
                else if (desc.Type == D3D_SIT_UAV_RWSTRUCTURED)
                {
                    attr_type = EShaderAttrType_RWStructuredBuffer;
                }

                mShaderAttribute.emplace_back(attr_type, desc.BindPoint, desc.BindCount, desc.Name);
            }
        }
    }

    const ShaderConstantBufferAttribute* D3D12ShaderCompilation::FindConstantBufferAttribute(std::string sematics_name) const
    {
        for (uint32 i = 0; i < GetConstantBufferCount(); i++)
        {
            if (mConstantBuffer[i].mName == sematics_name)
            {
                return &mConstantBuffer[i];
            };
        }

        return nullptr;
    }

    const ShaderAttribute* D3D12ShaderCompilation::FindAttribute(EShaderAttrType attr_type, std::string_view semantic_name) const
    {
        auto it = std::find_if(mShaderAttribute.begin(), mShaderAttribute.end(),
            [&](auto& attr)
            {
                return attr.mType == attr_type, attr.mName == semantic_name;
            }
        );

        if (it == mShaderAttribute.end())
        {
            return nullptr;
        }
        else
        {
            return &*it;
        }
    }

    // count how many attributes of the type in the shader
    uint32 D3D12ShaderCompilation::CountAttribute(EShaderAttrType type) const
    {
        size_t num = std::count_if(mShaderAttribute.begin(), mShaderAttribute.end(),
            [&](auto it)
            {
                return it.mType == type;
            }
        );

        return static_cast<uint32>(num);
    }

    // find the index-th attribute of the type in the shader
    const ShaderAttribute* D3D12ShaderCompilation::IndexAttribute(EShaderAttrType type, uint32 index) const
    {
        auto it = std::find_if(mShaderAttribute.begin(), mShaderAttribute.end(),
            [&](auto it)
            {
                return it.mType == type && index-- == 0;
            }
        );

        if (it == mShaderAttribute.end())
        {
            return nullptr;
        }
        else
        {
            return &*it;
        }
    }
}