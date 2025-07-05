#include "Resource/ResourceLoader.h"
#include "Fundation.h"
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

    std::shared_ptr<ModelResource> ResourceLoader::ImportModel(std::string_view file_path, std::string_view repo_path, float scale/*=1.0f*/, bool flip_uv_y/*=false*/)
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
                    .Normal = Vector3{
                        attrib.normals[3 * index.normal_index + 0],
                        attrib.normals[3 * index.normal_index + 1],
                        attrib.normals[3 * index.normal_index + 2],
                    }.GetNormalized(),
                    .Color = {1, 1, 1},
                    .TexCoord0 = {
                        attrib.texcoords[2 * index.texcoord_index + 0],
                        attrib.texcoords[2 * index.texcoord_index + 1],
                    }
                };

                if (flip_uv_y) 
                {
                    vertex.TexCoord0.y = 1.0f - vertex.TexCoord0.y;
                }

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
            vertex.Position = vertex.Position * scale;

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

            const float DEFAULT_METALLIC = 0.0f;
            const float DEFAULT_ROUGHNESS = 1.0f;
            const Vector3 DEFAULT_ALBEDO(1.0f, 1.0f, 1.0f);

            if (!obj_mat.diffuse_texname.empty()) 
            {
                std::filesystem::path albedo_tex_path = source_folder_path / obj_mat.diffuse_texname; // map_Kd
                std::shared_ptr<TextureResource> albedo_tex = ImportTexture(albedo_tex_path.string(), std::format("{}_{}", trimmed_path, obj_mat.diffuse_texname));
                
                material_resource->SetShaderParameter("UseAlbedoMap", ShaderParameter(static_cast<bool>(albedo_tex)));
                if (albedo_tex) 
                {
                    material_resource->SetTexture("AlbedoMap", albedo_tex);
                }
                else 
                {
                    material_resource->SetShaderParameter("Albedo", ShaderParameter(DEFAULT_ALBEDO));
                }
            }

            if (!obj_mat.normal_texname.empty())
            {
                std::filesystem::path normal_tex_path = source_folder_path / obj_mat.normal_texname; // norm
                std::shared_ptr<TextureResource> normal_tex = ImportTexture(normal_tex_path.string(), std::format("{}_{}", trimmed_path, obj_mat.normal_texname));

                material_resource->SetShaderParameter("UseNormalMap", ShaderParameter(static_cast<bool>(normal_tex)));
                if (normal_tex)
                {
                    material_resource->SetTexture("NormalMap", normal_tex);
                }
            }

            if (!obj_mat.roughness_texname.empty())
            {
                std::filesystem::path roughness_tex_path = source_folder_path / obj_mat.roughness_texname; // map_Pr
                std::shared_ptr<TextureResource> roughness_tex = ImportTexture(roughness_tex_path.string(), std::format("{}_{}", trimmed_path, obj_mat.roughness_texname));
                
                material_resource->SetShaderParameter("UseRoughnessMap", ShaderParameter(static_cast<bool>(roughness_tex)));
                if (roughness_tex)
                {
                    material_resource->SetTexture("RoughnessMap", roughness_tex);
                }
                else 
                {
                    material_resource->SetShaderParameter("Roughness", ShaderParameter(DEFAULT_ROUGHNESS));
                }
            }

            if (!obj_mat.metallic_texname.empty())
            {
                std::filesystem::path metallic_tex_path = source_folder_path / obj_mat.metallic_texname; // map_Pm
                std::shared_ptr<TextureResource> metallic_tex = ImportTexture(metallic_tex_path.string(), std::format("{}_{}", trimmed_path, obj_mat.metallic_texname));
                
                material_resource->SetShaderParameter("UseMetallicMap", ShaderParameter(true));
                if (metallic_tex)
                {
                    material_resource->SetTexture("MetallicMap", metallic_tex);
                }
                else
                {
                    material_resource->SetShaderParameter("Metallic", ShaderParameter(DEFAULT_METALLIC));
                }
            }

            if (!obj_mat.ambient_texname.empty())
            {
                std::filesystem::path ao_tex_path = source_folder_path / obj_mat.ambient_texname; // map_Ka
                std::shared_ptr<TextureResource> ao_tex = ImportTexture(ao_tex_path.string(), std::format("{}_{}", trimmed_path, obj_mat.ambient_texname));

                material_resource->SetShaderParameter("UseAmbientOcclusionMap", ShaderParameter(true));
                if (ao_tex)
                {
                    material_resource->SetTexture("AmbientOcclusionMap", ao_tex);
                }
            }

            mats.push_back(material_resource);
        }

        return std::make_shared<ModelResource>(std::format("{}_Model", trimmed_path), mesh_resource, mats);
    }

    std::shared_ptr<TextureResource> ResourceLoader::ImportTexture(std::string_view file_path, std::string_view repo_path, ETextureFormat foramt/*=ETextureFormat_None*/)
    {
        if (!std::filesystem::exists(file_path))
        {
            Log("File :", file_path, "Is Not Exist");
            return nullptr;
        }

        std::optional<TextureData> tex = LoadImageFile(file_path, foramt);
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

    std::optional<TextureData> ResourceLoader::LoadImageFile(std::string_view local_path, ETextureFormat format/*=ETextureFormat_None*/)
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
        if (extension == ".png" || extension == ".jpg")
        {
            return LoadWICImageFile(local_path, format);
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

    std::optional<TextureData> ResourceLoader::LoadWICImageFile(std::string_view local_path, ETextureFormat format/*=ETextureFormat_None*/)
    {
        DirectX::ScratchImage image;
        ThrowIfFailed(DirectX::LoadFromWICFile(ToWString(local_path).data(), DirectX::WIC_FLAGS_NONE, nullptr, image));

        const DirectX::Image* base_slice = image.GetImage(0, 0, 0);
        uint32 size = static_cast<uint32>(base_slice->rowPitch * base_slice->height);

        if ((base_slice->width % 4) != 0 || (base_slice->height % 4) != 0)
        {
            Warn(std::format("BC requires the width and height of the texture must be a multiple of 4, {} is not satisfied", local_path));
            return std::nullopt;
        }

        if (format != ETextureFormat_None) 
        {
            DirectX::ScratchImage temp;
            DirectX::Convert(*base_slice, static_cast<DXGI_FORMAT>(format), DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, temp);

            image = std::move(temp);
            base_slice = image.GetImage(0, 0, 0);
        }

        return GenerateImageMipmaps(base_slice);
    }

    std::optional<TextureData> ResourceLoader::LoadHDRImageFile(std::string_view local_path)
    {
        DirectX::ScratchImage image;
        HRESULT hr = DirectX::LoadFromHDRFile(
            ToWString(local_path).data(),
            nullptr,
            image
        );

        if (FAILED(hr)) {
            std::cerr << "Failed to load HDR file. Error: " << std::hex << hr << std::endl;
            CoUninitialize();
            return std::nullopt;
        }

        const DirectX::Image* base_slice = image.GetImage(0, 0, 0);
        uint32 size = static_cast<uint32>(base_slice->width * base_slice->height * DirectX::BitsPerPixel(base_slice->format) / CHAR_BIT);

        if ((base_slice->width % 4) != 0 || (base_slice->height % 4) != 0)
        {
            Warn(std::format("BC requires the width and height of the texture must be a multiple of 4, {} is not satisfied", local_path));
            return std::nullopt;
        }

        return GenerateImageMipmaps(base_slice);
    }

    std::array<TextureData, 6> ResourceLoader::LoadCubeMap(std::string_view filepath)
    {
        using std::filesystem::path;

        // same order as in the MSDN documentation
        // ref: https://learn.microsoft.com/en-us/windows/win32/direct3d9/cubic-environment-mapping
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

    std::optional<nlohmann::json> ResourceLoader::LoadJsonFile(std::string_view path)
    {
        std::optional<std::ifstream> file = ReadFile(path);
        if (!file.has_value()) 
        {
            return std::nullopt;
        }

        nlohmann::json::parser_callback_t cb = [](auto&&...) {
            return true;
        };

        try {
            nlohmann::json data = nlohmann::json::parse(file.value(), cb, true);
            return data;
        }
        catch (const nlohmann::json::parse_error& e) {
            Error("Json Parse Error: " , path);
            Error(e.what());
            ASSERT(false);
        }

        return std::nullopt;
    }

    TextureData ResourceLoader::GenerateImageMipmaps(const DirectX::Image* mip_0)
    {
        ASSERT(mip_0 && mip_0->width && mip_0->height);

        // generate mipmap by directxtex
        DirectX::ScratchImage mip_chain;
        ThrowIfFailed(GenerateMipMaps(*mip_0, DirectX::TEX_FILTER_DEFAULT, 0, mip_chain));

        // prepare container for the texture
        uint32 pixel_size = GetPixelSize(mip_0->format);
        uint32 mip_levels = static_cast<uint32>(mip_chain.GetImageCount());
        uint32 mip_0_width = static_cast<uint32>(mip_0->width);
        uint32 mip_0_height = static_cast<uint32>(mip_0->height);

        BinaryData binary_data(CalculateTextureSize(mip_0_width, mip_0_height, mip_levels, pixel_size));

        for (uint32 i = 0; i < mip_levels; i++)
        {
            // verify the mipmap size meets our expectation
            MipmapLayout layout = CalculateMipmapLayout(mip_0_width, mip_0_height, mip_levels, pixel_size, i);
            uint8* tex_base = static_cast<uint8*>(binary_data.GetData());
            uint8* mip_base = tex_base + layout.BaseOffset;
            
            const DirectX::Image* img = mip_chain.GetImage(i, 0, 0);

            ASSERT(img->rowPitch * img->height == layout.MipSize);
            if (i == mip_levels - 1) 
            {
                ASSERT(layout.BaseOffset + layout.MipSize == binary_data.GetSize());
            }

            // copy mipmap
            memcpy(mip_base, img->pixels, layout.MipSize);
        }

        return TextureData(
            std::move(binary_data),
            static_cast<uint16>(mip_0->height),
            static_cast<uint16>(mip_0->width),
            static_cast<uint16>(mip_levels),
            static_cast<ETextureFormat>(mip_0->format)
        );
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