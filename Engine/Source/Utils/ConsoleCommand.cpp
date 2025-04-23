#include <algorithm>

#include "Resource/ResourceLoader.h"
#include "Renderer/Device/Direct12/D3D12CommandList.h"
#include "Utils/SH.h"
#include "DirectXTex.h"
#include "Utils/ConsoleCommand.h"
#include "Resource/DefaultResource.h"

namespace MRenderer 
{
    void GenerateIrradianceMapCommand::Execute()
    {
        auto source_path = mParser.get<std::string>("file");
        auto dest_path = mParser.get<std::string>("output");
        auto debug = mParser.get<bool>("debug");

        if (source_path == "" || dest_path == "")
        {
            Log("Generation Failed, File Path Or Destination Path Is Empty");
            return;
        }

        std::shared_ptr<CubeMapResource> res = ResourceLoader::ImportCubeMap(source_path, dest_path);
        ASSERT(res);

        const uint32 IrradianceMapSize = 256;
        auto irradiance_map = SHBaker::GenerateIrradianceMap(res->mTextureData, IrradianceMapSize, debug);

        const std::string filename[] = {"posx", "negx", "posy", "negy", "posz", "negz"};
        uint32 pixel_size = irradiance_map[0].PixelSize();
        for (uint32 i = 0; i < 6; i++) 
        {
            using std::filesystem::path;
            std::string filepath = (path(dest_path) / filename[i]).replace_extension(".png").string();
            std::filesystem::create_directory(dest_path);

            DirectX::Image img;
            img.width = IrradianceMapSize;
            img.height = IrradianceMapSize;
            img.format = static_cast<DXGI_FORMAT>(irradiance_map[0].Format());
            img.rowPitch = IrradianceMapSize * pixel_size;
            img.slicePitch = IrradianceMapSize * IrradianceMapSize * pixel_size;
            img.pixels = static_cast<uint8*>(irradiance_map[i].mData.GetData());

            ThrowIfFailed(DirectX::SaveToWICFile(img, DirectX::WIC_FLAGS_NONE, DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), ToWString(filepath).c_str()));
        }

        Log("Irradiance Map Generation Finish, Resource Is Saved To ", dest_path);
    }

    void ImportModelCommand::Execute()
    {
        namespace fs = std::filesystem;
        fs::path source_path = mParser.get<std::string>("file");
        fs::path dest_path = mParser.get<std::string>("output");

        // generate @repo_path like this [Asset/Model/CigarBox => Asset/Model/CigarBox/CigarBox]
        // so all the resources are saved in the same folder, something like this:
        // Asset/Model/CigarBox/CigarBox.json
        // Asset/Model/CigarBox/CigarBox_Mat.json
        // Asset/Model/CigarBox/CigarBox_Texture.json
        auto repo_path = dest_path / dest_path.filename();

        if (source_path == "" || repo_path == "")
        {
            Log("Import Failed, File Path Or Destination Path Is Empty");
            return;
        }

        if (std::filesystem::exists(repo_path))
        {
            Log("Import Failed, Output Path Is Already Occupied");
            return;
        }

        std::shared_ptr<ModelResource> res = ResourceLoader::ImportModel(source_path.string(), repo_path.string());
        if (res) 
        {
            ResourceLoader::Instance().DumpResource(*res);
            Log("Import Model Finish, Resource Is Saved To ", repo_path);
        }
    }

    void ImportCubeMapCommand::Execute()
    {
        auto source_path = mParser.get<std::string>("file");
        auto dest_path = mParser.get<std::string>("output");

        if (source_path == "" || dest_path == "")
        {
            Log("Import Failed, File Path Or Destination Path Is Empty");
            return;
        }

        std::shared_ptr<CubeMapResource> res = ResourceLoader::ImportCubeMap(source_path, dest_path);
        ResourceLoader::Instance().DumpResource(*res);

        Log("Import Model Finish, Resource Is Saved To ", dest_path);
    }

    void CreateSphereModelCommand::Execute()
    {
        auto output_path = mParser.get<std::string>("output");
        if (output_path == "")
        {
            Log("Create Sphere Model Failed, Output Path Is Empty");
            return;
        }

        std::shared_ptr<ModelResource> res = DefaultResource::CreateStandardSphereModel(output_path);
        ResourceLoader::Instance().DumpResource(*res);
        Log("Create Sphere Model Finish, Resource Is Saved To ", output_path);
    }
}