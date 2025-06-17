#include "Resource/ResourceLoader.h"
#include "Fundation.h"
#include "DirectXTex.h"
#include "Resource/tiny_obj_loader.h"

#include <numeric>
#include <filesystem>


namespace MRenderer 
{
    ResourceLoader& ResourceLoader::Instance()
    {
        static ResourceLoader loader;
        return loader;
    }

    std::shared_ptr<ModelResource> ResourceLoader::ImportModel(std::string_view file_path, std::string_view repo_path)
    {
        using std::filesystem::path;

        path source_path = path(file_path);
        path source_folder_path = source_path.parent_path();

        if (!std::filesystem::exists(file_path))
        {
            Log("File :", file_path, "Is Not Exist");
            return nullptr;
        }

        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn, err;

        bool res = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, file_path.data(), source_folder_path.string().data());

        if (!warn.empty())
        {
            Log(warn);
        }

        if (!err.empty())
        {
            Log(err);
        }

        if (!res) {
            Log("tinyobj::LoadObj Failed ", file_path);
            return nullptr;
        }

        //ref: https://vulkan-tutorial.com/Loading_models
        // meshes's size is equal to the size of materials, each element is a vertex group of the material
        std::vector<std::vector<StandardVertex>> meshes; 
        meshes.resize(materials.size());
        Vector3 center;
        AABB bound;

        // collect each shape's vertex data, two triangles are in a same mesh if they have the same material_ids
        for(auto& shape : shapes)
        {
            for (uint32 i = 0; i < shape.mesh.indices.size(); i++) 
            {
                // since there are three material_ids for a single triangle, we will use the first vertex's material_ids as the material of this triangle
                uint32 mat_index = shape.mesh.material_ids[i / 3];

                auto index = shape.mesh.indices[i];

                StandardVertex vertex = {
                    // float array to vector3
                    .Position = {
                        attrib.vertices[3 * index.vertex_index + 0],
                        attrib.vertices[3 * index.vertex_index + 1],
                        attrib.vertices[3 * index.vertex_index + 2],
                    },
                    .Normal = {
                        attrib.normals[3 * index.normal_index + 0],
                        attrib.normals[3 * index.normal_index + 1],
                        attrib.normals[3 * index.normal_index + 2],
                    },
                    .Color = {1, 1, 1},
                    .TexCoord0 = {
                        attrib.texcoords[2 * index.texcoord_index + 0],
                        attrib.texcoords[2 * index.texcoord_index + 1],
                    }
                };

                center = center + vertex.Position;
                meshes[mat_index].push_back(vertex);
            }
        }

        // cacluate tangent
        for (uint32 mesh_idx = 0; mesh_idx < meshes.size(); mesh_idx++) 
        {
            for(uint32 vertex_idx = 0; vertex_idx < meshes[mesh_idx].size(); vertex_idx += 3) 
            {
                StandardVertex& v0 = meshes[mesh_idx][vertex_idx];
                StandardVertex& v1 = meshes[mesh_idx][vertex_idx + 1];
                StandardVertex& v2 = meshes[mesh_idx][vertex_idx + 2];

                Vector3 tangent = CalculateTangent(v0.Position, v1.Position, v2.Position, v0.TexCoord0, v1.TexCoord0, v2.TexCoord0);

                v0.Tangent = tangent;
                v1.Tangent = tangent;
                v2.Tangent = tangent;
            }
        }

        std::vector<StandardVertex> vertices;
        std::vector<uint32> indicies;
        std::vector<SubMeshData> sub_meshes;

        // record sub mesh info, i.e the begin index and the count of the indices for each mesh
        uint32 index_begin = 0;
        for (auto& mesh : meshes) 
        {
            uint32 num_mesh_vertex = static_cast<uint32>(mesh.size());
            sub_meshes.push_back(SubMeshData{.Index = index_begin, .IndicesCount = num_mesh_vertex });
            index_begin += num_mesh_vertex;

            vertices.insert(vertices.end(), mesh.begin(), mesh.end());
        }

        // make the model vertex near to the origin
        center = center / static_cast<float>(vertices.size());
        for (auto& vertex : vertices)
        {
            vertex.Position = vertex.Position - center;
            vertex.Position = vertex.Position * 0.01F;

            // calculate the bound on the fly
            bound.Min = Vector3::Min(bound.Min, vertex.Position);
            bound.Max = Vector3::Max(bound.Max, vertex.Position);
        };

        // generate indicies, no vertex sharing for now
        indicies.resize(vertices.size());
        std::iota(indicies.begin(), indicies.end(), 0);

        std::string trimmed_path = std::filesystem::path(repo_path).replace_extension("").string(); // trim extension
        auto mesh_resource = std::make_shared<MeshResource>(
            trimmed_path + "_Mesh",
            MeshData(StandardVertexFormat, vertices, indicies, sub_meshes, bound)
        );
        
        // collect material and textures
        std::vector<std::shared_ptr<MaterialResource>> mats;
        for (uint32 i = 0; i < materials.size(); i++) 
        {
            auto& obj_mat = materials[i];
            auto material_resource = std::make_shared<MaterialResource>(std::format("{}_Mat_{}", trimmed_path, std::to_string(i)));
            material_resource->SetShader("gbuffer.hlsl");

            if (!obj_mat.diffuse_texname.empty()) 
            {
                std::filesystem::path base_color_path = source_folder_path / obj_mat.diffuse_texname;
                std::shared_ptr<TextureResource> base_color = ImportTexture(base_color_path.string(), std::format("{}_{}", trimmed_path, obj_mat.diffuse_texname));
                if (base_color) 
                {
                    material_resource->SetTexture("BaseColorMap", base_color);
                }
            }

            if (!obj_mat.bump_texname.empty())
            {
                std::filesystem::path normal_path = source_folder_path / obj_mat.bump_texname;
                std::shared_ptr<TextureResource> normal = ImportTexture(normal_path.string(), std::format("{}_{}", trimmed_path, obj_mat.bump_texname));
                if (normal) 
                {
                    material_resource->SetTexture("NormalMap", normal);
                }
            }

            if (!obj_mat.specular_texname.empty())
            {
                std::filesystem::path normal_path = source_folder_path / obj_mat.specular_texname;
                std::shared_ptr<TextureResource> normal = ImportTexture(normal_path.string(), std::format("{}_{}", trimmed_path, obj_mat.specular_texname));
                if (normal)
                {
                    material_resource->SetTexture("MixedMap", normal);
                }
            }

            mats.push_back(material_resource);
        }

        return std::make_shared<ModelResource>(std::format("{}_{}", trimmed_path, "Model"), mesh_resource, mats);
    }

    std::shared_ptr<TextureResource> ResourceLoader::ImportTexture(std::string_view file_path, std::string_view repo_path)
    {
        if (!std::filesystem::exists(file_path))
        {
            Log("File :", file_path, "Is Not Exist");
            return nullptr;
        }

        std::optional<TextureData> tex = LoadImageFile(file_path);
        if (!tex) 
        {
            return nullptr;
        }

        auto res = std::make_shared<TextureResource>(repo_path, std::move(tex.value()));
        return res;
    }

    std::shared_ptr<CubeMapResource> ResourceLoader::ImportCubeMap(std::string_view file_path, std::string_view repo_path)
    {
        using std::filesystem::path;

        if (!std::filesystem::exists(file_path))
        {
            Log("File :", file_path, "Is Not Exist");
            return nullptr;
        }

        std::array<TextureData, 6> cube_map =  LoadCubeMap(file_path);

        auto ret = std::make_shared<CubeMapResource>(repo_path, std::move(cube_map));
        return ret;
    }

    std::optional<TextureData> ResourceLoader::LoadImageFile(std::string_view local_path)
    {
        using namespace DirectX;
        using std::filesystem::path;

        // initialize DirectXTex library
        // ref: https://github.com/microsoft/DirectXTex/wiki/DirectXTex
        static bool _ =
            []()
            {
                ThrowIfFailed(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
                return true;
            }();

        path extension = path(local_path).extension();
        if (extension == ".png") 
        {
            return LoadPNGImageFile(local_path);
        }
        else if(extension == ".hdr")
        {
            return LoadHDRImageFile(local_path);
        }
        else 
        {
            ASSERT(false && "Not Implemented");
            return std::nullopt;
        }
    }

    std::optional<TextureData> ResourceLoader::LoadPNGImageFile(std::string_view local_path)
    {
        using namespace DirectX;

        ScratchImage container;
        ThrowIfFailed(DirectX::LoadFromWICFile(ToWString(local_path).data(), WIC_FLAGS_NONE, nullptr, container));

        const Image* image = container.GetImage(0, 0, 0);
        uint32 size = static_cast<uint32>(image->width * image->height * DirectX::BitsPerPixel(image->format) / CHAR_BIT);

        if ((image->width % 4) != 0 || (image->height % 4) != 0)
        {
            Warn(std::format("BC requires the width and height of the texture must be a multiple of 4, {} is not satisfied", local_path));
            return std::nullopt;
        }

        return TextureData(
            BinaryData(image->pixels, size),
            static_cast<uint32>(image->width),
            static_cast<uint32>(image->height),
            static_cast<ETextureFormat>(image->format)
        );
    }

    std::optional<TextureData> ResourceLoader::LoadHDRImageFile(std::string_view local_path)
    {
        using namespace DirectX;

        DirectX::ScratchImage container;
        HRESULT hr = DirectX::LoadFromHDRFile(
            ToWString(local_path).data(),
            nullptr,
            container
        );

        if (FAILED(hr)) {
            std::cerr << "Failed to load HDR file. Error: " << std::hex << hr << std::endl;
            CoUninitialize();
            return std::nullopt;
        }

        const DirectX::Image* image = container.GetImage(0, 0, 0);
        uint32 size = static_cast<uint32>(image->width * image->height * DirectX::BitsPerPixel(image->format) / CHAR_BIT);

        if ((image->width % 4) != 0 || (image->height % 4) != 0)
        {
            Warn(std::format("BC requires the width and height of the texture must be a multiple of 4, {} is not satisfied", local_path));
            return std::nullopt;
        }

        static int i = 0;
        SaveToHDRFile(*image, std::format(L"{}{}{}", L"F:/dev/test", i++, L".hdr").data());

        return TextureData(
            BinaryData(image->pixels, size),
            static_cast<uint32>(image->width),
            static_cast<uint32>(image->height),
            static_cast<ETextureFormat>(image->format)
        );
    }

    std::array<TextureData, 6> ResourceLoader::LoadCubeMap(std::string_view filepath)
    {
        using std::filesystem::path;

        std::array<TextureData, 6> texture_data;
        const char* file_names[] = { "px.hdr", "nx.hdr", "py.hdr", "ny.hdr", "pz.hdr", "nz.hdr" };
        for (uint32 i = 0; i < 6; i++)
        {
            path local_path = filepath / path(file_names[i]);
            ASSERT(std::filesystem::exists(local_path));

            std::optional<TextureData> tex = LoadImageFile(local_path.string());
            ASSERT(tex.has_value());

            texture_data[i] = std::move(tex.value());
        }
        return texture_data;
    }

    // ref: introductionto 3d game programming with directx12 19.3
    Vector3 ResourceLoader::CalculateTangent(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector2& t0, const Vector2& t1, const Vector2& t2)
    {
        Vector3 e1 = p1 - p0;
        Vector3 e2 = p2 - p0;
        Vector2 duv1 = t1 - t0;
        Vector2 duv2 = t2 - t0;

        float det = duv1.x * duv2.y - duv2.x * duv1.y;
        if (det < 0.0001) 
        {
            return Vector3(1, 0, 0);
        }

        Vector3 tangent = 
        {
            (duv2.y * e1.x - duv1.y * e2.x) / det,
            (duv2.y * e1.y - duv1.y * e2.y) / det,
            (duv2.y * e1.z - duv1.y * e2.z) / det
        };

        return tangent.GetNormalized();
    }
}