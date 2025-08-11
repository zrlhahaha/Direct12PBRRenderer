#include "Resource/ResourceDef.h"
#include "DirectXTex.h"
#include "Utils/Serialization.h"
#include "Resource/Shader.h"
#include "Resource/ResourceLoader.h"
#include "Resource/TextureCompression.h"
#include "Renderer/Device/Direct12/D3D12Device.h"
#include "Resource/json.hpp"
#include "format"

namespace MRenderer
{
    void MeshResource::AllocateGPUResource()
    {
        MeshData mesh_data;
        ResourceLoader::Instance().LoadBinary(mesh_data, mMeshPath);

        mBound = mesh_data.Bound();
        mVertexFormat = mesh_data.Format();
        mSubMeshes = mesh_data.GetSubMeshs();

        VertexDefination layout = GetVertexLayout(mesh_data.Format());
        mDeviceVertexBuffer = GD3D12Device->CreateVertexBuffer(
            mesh_data.Vertices().GetData(),
            mesh_data.Vertices().GetSize(),
            layout.VertexSize
        );

        mDeviceIndexBuffer = GD3D12Device->CreateIndexBuffer(
            mesh_data.Indicies().GetData(),
            mesh_data.Indicies().GetSize()
        );
    }


    MeshResource::MeshResource(std::string_view repo_path, std::string_view mesh_path)
        :IResource(), mMeshPath(mesh_path)
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
        TextureData tex;
        ASSERT(ResourceLoader::Instance().LoadBinary(tex, mTexturePath));

        mDeviceTexture = GD3D12Device->CreateTexture2D(
            tex.Width(),
            tex.Height(),
            tex.MipLevels(),
            tex.Format(),
            ETexture2DFlag_None,
            tex.DataSize(),
            tex.Data()
        );
    }

    std::optional<ShaderParameter> MaterialResource::GetShaderParameter(const std::string& name)
    { 
        auto it = mParameterTable.find(name);
        if (it == mParameterTable.end()) 
        {
            return std::nullopt;
        }
        else
        {
            return it->second;
        }
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

    void CubeMapResource::PostDeserialized()
    {
        CubeMapTextureData texture = ReadTextureFile();
        AllocateGPUResource(texture);
    }

    void CubeMapResource::AllocateGPUResource(const CubeMapTextureData& texture)
    {
        const TextureData& face0 = texture.Data()[0]; // texture format of 6 faces are the same.

        ASSERT(
            texture.Data()[0].mInfo == texture.Data()[1].mInfo && texture.Data()[1].mInfo == texture.Data()[2].mInfo &&
            texture.Data()[2].mInfo == texture.Data()[3].mInfo && texture.Data()[3].mInfo == texture.Data()[4].mInfo &&
            texture.Data()[4].mInfo == texture.Data()[5].mInfo
        );

        std::array<const void*, NumCubeMapFaces> pixels{};
        for (uint32 i = 0; i < NumCubeMapFaces; i++) 
        {
            pixels[i] = texture.Data()[i].Data();
        }

        mDeviceTexture2DArray = GD3D12Device->CreateTextureCube(face0.Width(), face0.Height(), face0.MipLevels(), face0.Format(), false, face0.DataSize(), &pixels);
        mDeviceTexture2DArray->Resource()->SetName(L"CubeMap");
        mSHCoefficients = texture.mSHCoefficients;
    }

    CubeMapTextureData CubeMapResource::ReadTextureFile()
    {
        CubeMapTextureData texture;
        ResourceLoader::Instance().LoadBinary(texture, mTexturePath);
        return texture;
    }
}