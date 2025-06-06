#pragma once
#include <string>

#include "Resource/ResourceDef.h"
#include "Utils/Serialization.h"




namespace MRenderer
{    
    template<typename T>
    concept Resource = std::is_base_of_v<IResource, T> && !std::is_same_v<IResource, T>;


    class ResourceLoader 
    {
    public:
        static ResourceLoader& Instance();

        static std::shared_ptr<ModelResource> ImportModel(std::string_view file_path, std::string_view repo_path);
        static std::shared_ptr<TextureResource> ImportTexture(std::string_view file_path, std::string_view repo_path);
        static std::shared_ptr<CubeMapResource> ImportCubeMap(std::string_view file_path, std::string_view repo_path);
        
        static std::optional<TextureData> LoadImageFile(std::string_view path);
        static std::optional<TextureData> LoadPNGImageFile(std::string_view local_path);
        static std::optional<TextureData> LoadHDRImageFile(std::string_view local_path);
        static std::array<TextureData, 6> LoadCubeMap(std::string_view path);

        static Vector3 CalculateTangent(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector2& t0, const Vector2& t1, const Vector2& t2);
        
        template<typename T>
        static std::string ActualFilePath(std::string_view repo_path) 
        {
            return std::filesystem::path(repo_path).replace_extension(GetResourceExtension(T::ResourceFormat)).string();
        }

        // Serialize a resource object of type T as a binary file in the specified folder path.
        template<Resource T>
        bool DumpBinaryResource(T& resource, std::string_view repo_path)
        {
            BinarySerializer serializer;
            serializer.LoadObject(resource);
            return serializer.DumpFile(repo_path);
        }

        // Create a resource object of type T from a binary file.
        template<Resource T>
        bool LoadBinaryResource(T& out_resource, std::string_view repo_path)
        {
            BinarySerializer serializer;
            if (serializer.LoadFile(repo_path))
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

        template<Resource T>
        bool DumpJsonResource(T& resource, std::string_view repo_path)
        {
            std::string folder_path = std::filesystem::path(repo_path).parent_path().string();
            ASSERT(std::filesystem::is_directory(folder_path) || std::filesystem::create_directories(folder_path));

            std::ofstream file;
            file.open(repo_path, std::ios::out);

            if (!file.is_open())
            {
                Log("Failed To Create File At ", repo_path);
                return false;
            }

            nlohmann::json json;
            JsonSerialization::Serialize(json, resource);


            file << json.dump(4) << std::endl;
            file.close();
            return true;
        }

        template<Resource T>
        bool LoadJsonResource(T& resource, std::string_view file_path)
        {
            std::optional<std::ifstream> file = LoadFile(file_path);
            ASSERT(file.has_value());

            std::ifstream& in = file.value();

            nlohmann::json json;
            in >> json;
            JsonSerialization::Deserialize(json, resource);

            in.close();
            return true;
        }

        template<Resource T>
            requires std::is_base_of_v<IResource, T>
        std::shared_ptr<T> LoadResource(std::string_view repo_path)
        {
            using std::filesystem::path;
            auto file_path = ActualFilePath<T>(repo_path);

            // resource is cached
            const auto& it = mResourceCache.find(repo_path.data());
            if (it != mResourceCache.end()) 
            {
                return std::dynamic_pointer_cast<T>(it->second);
            }

            // load from disk
            static_assert(std::is_default_constructible_v<T>);
            std::shared_ptr<T> res = nullptr;

            if constexpr(T::ResourceFormat == EResourceFormat_Json)
            {                
                res = std::make_shared<T>();
                res->SetRepoPath(repo_path);
                if (!LoadJsonResource(*res, file_path))
                {
                    res = nullptr;
                }
            }
            else if constexpr (T::ResourceFormat == EResourceFormat_Binary)
            {
                res = std::make_shared<T>();
                res->SetRepoPath(repo_path);
                if (!LoadBinaryResource(*res, file_path))
                {
                    res = nullptr;
                }
            }
            else 
            {
                UNEXPECTED("Unknow Resource Format");
            }

            ASSERT(res);
            if(res)
            {
                mResourceCache[repo_path.data()] = res;
                return res;
            }
            else 
            {
                return nullptr;
            }
        }

        template<Resource T>
        void DumpResource(T& res)
        {
            auto file_path = ActualFilePath<T>(res.GetRepoPath());
            if constexpr (T::ResourceFormat == EResourceFormat_Json)
            {
                DumpJsonResource(res, file_path);
            }
            else if constexpr (T::ResourceFormat == EResourceFormat_Binary)
            {
                DumpBinaryResource(res, file_path);
            }
            else
            {
                UNEXPECTED("Unknow Resource Format");
            }
        }
    protected:
        ResourceLoader() = default;

    protected:
        std::unordered_map<std::string, std::shared_ptr<IResource>> mResourceCache;
    };
}