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
            reinterpret_cast<uint32*>(mMeshData.mIndicies.GetData()),
            mMeshData.mIndicies.GetSize()
        );

        // release data
        mMeshData.mVertices = BinaryData();
        mMeshData.mIndicies = BinaryData();
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
        ASSERT(!mTextureData.Empty());

        mDeviceTexture = GD3D12Device->CreateTexture2D(
            mTextureData.Width(),
            mTextureData.Height(),
            mTextureData.MipLevels(),
            mTextureData.Format(),
            false,
            mTextureData.DataSize(),
            mTextureData.Data()
        );

        // release the texture memory
        mTextureData.mData = BinaryData();
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
        const TextureData& face0 = mTextureData[0]; // texture format of 6 faces are the same.

        std::array<const void*, NumCubeMapFaces> pixels{};
        for (uint32 i = 0; i < NumCubeMapFaces; i++) 
        {
            ASSERT(!mTextureData[i].Empty());
            pixels[i] = mTextureData[i].Data();
        }

        mDeviceTexture2DArray = GD3D12Device->CreateTextureCube(face0.Width(), face0.Height(), face0.MipLevels(), face0.Format(), false, face0.DataSize(), &pixels);
        mDeviceTexture2DArray->Resource()->SetName(L"CubeMap");

        for (uint32 i = 0; i < NumCubeMapFaces; i++)
        {
            mTextureData[i].mData = BinaryData();
        }
    }
}