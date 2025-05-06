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
}