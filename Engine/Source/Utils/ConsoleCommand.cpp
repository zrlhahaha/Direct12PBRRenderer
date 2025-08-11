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
            Log("Generation failed, File path or destination path is empty");
            return;
        }

        std::shared_ptr<CubeMapResource> res = ResourceLoader::ImportCubeMap(source_path, dest_path);
        ASSERT(res);

        const uint32 IrradianceMapSize = 256;
        auto irradiance_map = SHBaker::GenerateIrradianceMap(res->ReadTextureFile().Data(), IrradianceMapSize, debug);

        const std::string filename[] = {"posx", "negx", "posy", "negy", "posz", "negz"};
        uint32 pixel_size = irradiance_map[0].PixelSize();
        for (uint32 i = 0; i < 6; i++) 
        {
            using std::filesystem::path;
            std::string filepath = (path(dest_path) / filename[i]).replace_extension(".hdr").string();
            std::filesystem::create_directory(dest_path);

            DirectX::Image img;
            img.width = IrradianceMapSize;
            img.height = IrradianceMapSize;
            img.format = static_cast<DXGI_FORMAT>(irradiance_map[0].Format());
            img.rowPitch = IrradianceMapSize * pixel_size;
            img.slicePitch = IrradianceMapSize * IrradianceMapSize * pixel_size;
            img.pixels = static_cast<uint8*>(irradiance_map[i].mData.GetData());

            ThrowIfFailed(DirectX::SaveToHDRFile(img, ToWString(filepath).c_str()));
        }

        Log("Irradiance map generation Finish, Resource is saved to ", dest_path);
    }

    void ImportModelCommand::Execute()
    {
        namespace fs = std::filesystem;
        fs::path source_path = mParser.get<std::string>("file");
        fs::path dest_path = mParser.get<std::string>("output");
        float model_scale = mParser.get<float>("scale");
        bool flip_uv_y = mParser.get<bool>("flip_uv_y");

        // generate @repo_path like this [Asset/Model/CigarBox => Asset/Model/CigarBox/CigarBox]
        // so all the resources are saved in the same folder, something like this:
        // Asset/Model/CigarBox/CigarBox.json
        // Asset/Model/CigarBox/CigarBox_Mat.json
        // Asset/Model/CigarBox/CigarBox_Texture.json
        auto repo_path = dest_path / dest_path.filename();

        if (source_path == "" || repo_path == "")
        {
            Log("Import failed, File path or destination path is empty");
            return;
        }

        if (std::filesystem::exists(repo_path))
        {
            Log("Import failed, Output path Is already occupied");
            return;
        }

        ResourceLoader::ImportModel(source_path.string(), repo_path.string(), model_scale, flip_uv_y);
        Log("Import finish, Resource is saved to", repo_path);
    }


    void MRenderer::ImportTextureCommand::Execute()
    {
        namespace fs = std::filesystem;
        fs::path source_path = mParser.get<std::string>("file");
        fs::path dest_path = mParser.get<std::string>("output");
        int format = mParser.get<int>("format");

        if (source_path == "" || dest_path == "")
        {
            Log("Import failed, file Path or destination path is empty");
            return;
        }

        if (std::filesystem::exists(dest_path))
        {
            Log("Import failed, Output path is already occupied");
            return;
        }

        ResourceLoader::ImportTexture(source_path.string(), dest_path.string(), static_cast<ETextureFormat>(format));
        Log("Import finish, Resource is saved to", dest_path);
    }

    void ImportCubeMapCommand::Execute()
    {
        auto source_path = mParser.get<std::string>("folder");
        auto dest_path = mParser.get<std::string>("output");

        if (source_path == "" || dest_path == "")
        {
            Log("Import failed, File path or destination path is empty");
            return;
        }

        ResourceLoader::ImportCubeMap(source_path, dest_path);

        Log("Import finish, Resource is saved to ", dest_path);
    }

    void CreateSphereModelCommand::Execute()
    {
        auto output_path = mParser.get<std::string>("output");
        if (output_path == "")
        {
            Log("Create sphere model failed, output path is empty");
            return;
        }

        std::shared_ptr<ModelResource> res = ResourceLoader::CreateStandardSphereModel(output_path);
        ResourceLoader::Instance().DumpResource(*res);
        Log("Create sphere model finish, Resource is saved to ", output_path);
    }

    // note: command is executed on worker thread
    void CommandExecutor::StartReceivingCommand()
    {
        auto future = TaskScheduler::Instance().ExecuteOnWorker(
            [&]() -> void {
                const uint32 CommandStringBufferSize = 500;

                while (true)
                {
                    char cmd[CommandStringBufferSize];
                    std::cin.getline(cmd, CommandStringBufferSize);

                    size_t size = strlen(cmd);
                    ASSERT(size <= CommandStringBufferSize);

                    std::string line(cmd, size);
                    size_t index = line.find(' ');


                    if (!line.empty())
                    {
                        // execute command
                        std::string cmd_str;
                        if (index == std::string::npos)
                        {
                            cmd_str = line;
                        }
                        else
                        {
                            cmd_str = line.substr(0, index);
                        }

                        auto future = TaskScheduler::Instance().ExecuteOnMainThread(
                            [=, this]()
                            {
                                ExecuteCommand(cmd_str, line);
                            }
                        );

                        // wait until the command is done
                        future.wait();
                    }
                    else
                    {
                        // or log usage
                        for (auto& it : mCommandMap)
                        {
                            Log(it.first);
                            Log(it.second->mParser.usage());
                        }
                    }
                }
            }
        );
    }
}