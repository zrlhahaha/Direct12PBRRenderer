#pragma once
#include <string>
#include "DirectXTex.h"
#include "Resource/ResourceDef.h"
#include "Utils/Serialization.h"


namespace MRenderer
{
    template<typename T>
    concept ResourceClass = ReflectedClass<T> && std::is_default_constructible_v<T> && std::is_base_of_v<IResource, T>;

    class ResourceLoader 
    {
    public:
        static ResourceLoader& Instance();

        // import .obj file
        static std::shared_ptr<ModelResource> ImportModel(std::string_view file_path, std::string_view repo_path, float scale = 1.0f, bool flip_uv_y=false);
        
        // import .jpg .png .hdr image
        static std::shared_ptr<TextureResource> ImportTexture(std::string_view file_path, std::string_view repo_path, ETextureFormat foramt=ETextureFormat_None);

        // import cubemap folder
        static std::shared_ptr<CubeMapResource> ImportCubeMap(std::string_view file_path, std::string_view repo_path);

        // create unit sphere model
        static std::shared_ptr<ModelResource> CreateStandardSphereModel(std::string_view repo_path);
        
        static std::optional<TextureData> LoadImageFile(std::string_view path, ETextureFormat foramt=ETextureFormat_None);
        static std::optional<TextureData> LoadWICImageFile(std::string_view local_path, ETextureFormat format=ETextureFormat_None);
        static std::optional<TextureData> LoadHDRImageFile(std::string_view local_path);
        static std::array<TextureData, NumCubeMapFaces> LoadCubeMap(std::string_view path);
        static std::optional<nlohmann::json> LoadJsonFile(std::string_view path);

        static Vector3 CalculateTangent(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector2& t0, const Vector2& t1, const Vector2& t2);

        // Serialize a resource object of type T as a binary file in the specified folder path.
        template<ReflectedClass T>
        bool DumpBinary(T& resource, std::string_view repo_path)
        {
            // create parent folder if it's not existed
            std::string folder_path = std::filesystem::path(repo_path).parent_path().string();
            ASSERT(std::filesystem::is_directory(folder_path) || std::filesystem::create_directories(folder_path));

            // write binary file
            namespace fs = std::filesystem;
            fs::path file_path = fs::path(repo_path).replace_extension(".bin");

            BinarySerializer serializer;
            serializer.LoadObject(resource);
            return serializer.DumpFile(file_path.string());
        }

        // Create a resource object of type T from a binary file.
        template<ReflectedClass T>
        bool LoadBinary(T& out_resource, std::string_view repo_path)
        {
            namespace fs = std::filesystem;
            fs::path file_path = fs::path(repo_path).replace_extension(".bin");

            BinarySerializer serializer;
            if (serializer.LoadFile(file_path.string()))
            {
                serializer.DumpObject(out_resource);
                ASSERT(serializer.Size() == 0);
                return true;
            }
            else
            {
                return false;
            }
        };

        template<ReflectedClass T>
        bool DumpJson(T& resource, std::string_view repo_path)
        {
            // create parent folder if it's not existed
            std::string folder_path = std::filesystem::path(repo_path).parent_path().string();
            ASSERT(std::filesystem::is_directory(folder_path) || std::filesystem::create_directories(folder_path));

            // open file handler
            namespace fs = std::filesystem;
            fs::path file_path = fs::path(repo_path).replace_extension(".json");

            std::optional<std::ofstream> file = WriteFile(file_path.string());
            ASSERT(file.has_value());

            // do the serialization
            nlohmann::json json;
            JsonSerialization::Serialize(json, resource);

            // dump the json file
            file.value() << json.dump(4) << std::endl;
            return true;
        }

        template<ReflectedClass T>
        bool LoadJson(T& resource, std::string_view repo_path)
        {
            // open file handler
            namespace fs = std::filesystem;
            fs::path file_path = fs::path(repo_path).replace_extension(".json");

            // load the json file
            std::optional<nlohmann::json> json = LoadJsonFile(file_path.string());
            ASSERT(json.has_value());

            // deserialize the json file and return the @resource
            JsonSerialization::Deserialize(json.value(), resource);
            return true;
        }

        template<ResourceClass T>
        std::shared_ptr<T> LoadResource(std::string_view repo_path)
        {

            TimeScope _scope(std::format("LoadResource {}", repo_path));

            auto it = mResourceCache.find(repo_path);
            if (it != mResourceCache.end())
            {
                std::shared_ptr<T> ret = std::static_pointer_cast<T>(it->second);
                ASSERT(ret);
                return ret;
            }

            // deserialized it from json file
            std::shared_ptr<T> resource = std::make_shared<T>();
            LoadJson(*resource, repo_path);

            // cache the resource
            mResourceCache[repo_path] = resource;
            resource->SetRepoPath(repo_path);
            return resource;
        }

        template<ResourceClass T>
        bool DumpResource(T& res)
        {
            return DumpJson(res, res.GetRepoPath());
        }

    protected:
        ResourceLoader() = default;

        static std::string GenerateDataPath(std::string_view path);
        static TextureData GenerateImageMipmaps(const DirectX::Image* mip_0);

    protected:
        std::unordered_map<std::string_view, std::shared_ptr<IResource>> mResourceCache;
    };
}