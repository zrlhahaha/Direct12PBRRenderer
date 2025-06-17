#pragma once
#include <type_traits>
#include <fstream>
#include <filesystem>
#include <string>

#include "Resource/json.hpp"
#include "Misc.h"
#include "Utils/ReflectionDef.h"

namespace MRenderer 
{
    // check if @T has custom binary serialization and deserialization function
    template<typename T>
    concept CustomBinarytSerializable = requires(T& t, RingBuffer rb) {
        T::BinarySerialize(rb, const_cast<const T&>(t));
        T::BinaryDeserialize(rb, t);
    };

    // check if @T has custom json serialization and deserialization function
    template<typename T>
    concept CustomJsonSerializable = requires(nlohmann::json & data, T & t) {
        T::JsonSerialize(data, const_cast<const T&>(t));
        T::JsonDeserialize(data, t);
    };

    // check if @T has PostDeserialized function
    template<typename T>
    concept IPostDeserialized = requires(T& t) {
        t.PostDeserialized();
    };

    // check if @T has PostSerialized function
    template<typename T>
    concept IPostSerialized = requires(const T& t) {
        t.PostSerialized();
    };
}

inline std::string FormatBaseClassString(std::string_view name) 
{
    return std::format("@{}", name);
}

namespace MRenderer::BinarySerialization
{
    // forward declaration
    template<ReflectedClass T>
        requires (!CustomBinarytSerializable<T>)
    void Serialize(RingBuffer& rb, const T& t);

    template<ReflectedClass T>
        requires (!CustomBinarytSerializable<T>)
    void Deserialize(RingBuffer& rb, T& t);

    //for basic type
    template<typename T>
        requires (std::is_arithmetic_v<T> && !ReflectedEnum<T>)
    void Serialize(RingBuffer& rb, const T& t)
    {
        rb.Write(reinterpret_cast<const uint8*>(&t), sizeof(T));
    }

    template<typename T>
        requires (std::is_arithmetic_v<T> && !ReflectedEnum<T>)
    void Deserialize(RingBuffer& rb, T& t)
    {
        const uint8* data = rb.Read(sizeof(T));
        memcpy(&t, data, sizeof(T));
    }

    // for reflected enum
    template<ReflectedEnum T>
    void Serialize(RingBuffer& rb, const T& t)
    {
        Serialize(rb, static_cast<const uint32&>(t));
    }

    template<ReflectedEnum T>
    void Deserialize(RingBuffer& rb, T& t)
    {
        uint32 val;
        Deserialize(rb, val);
        t = static_cast<T>(val);
    }

    // custom serialize & deserialize function
    template<typename T>
        requires CustomBinarytSerializable<T>
    void Serialize(RingBuffer& rb, const T& t)
    {
        T::BinarySerialize(rb, t);
    }

    template<typename T>
        requires CustomBinarytSerializable<T>
    void Deserialize(RingBuffer& rb, T& t)
    {
        T::BinaryDeserialize(rb, t);
    }

    // for std::string std::vector
    template<typename T>
        requires is_specialization_v<T, std::vector> || is_specialization_v<T, std::basic_string>
    void Serialize(RingBuffer& rb, const T& vec)
    {
        rb.Write(static_cast<uint32>(vec.size()));
        for (auto& it : vec)
        {
            Serialize(rb, it);
        }
    }

    template<typename  T>
        requires is_specialization_v<T, std::vector> || is_specialization_v<T, std::basic_string>
    void Deserialize(RingBuffer& rb, T& t)
    {
        uint32 vec_size = rb.Read<uint32>();
        ASSERT(vec_size < 65535);

        t.resize(vec_size);
        for (auto& it : t)
        {
            Deserialize(rb, it);
        }
    }

    // for std::array
    template<typename T>
        requires is_array_v<T>
    void Serialize(RingBuffer& rb, const T& t)
    {
        for (auto& it : t)
        {
            Serialize(rb, it);
        }
    }

    template<typename  T>
        requires is_array_v<T>
    void Deserialize(RingBuffer& rb, T& t)
    {
        for (auto& it : t)
        {
            Deserialize(rb, it);
        }
    }

    // for smart pointer
    template<typename T>
        requires is_specialization_v<T, std::shared_ptr> || is_specialization_v<T, std::unique_ptr>
    void Serialize(RingBuffer& rb, const T& t)
    {
        ASSERT(t.get());
        Serialize(rb, *t);
    }

    template<typename T>
        requires is_specialization_v<T, std::shared_ptr> || is_specialization_v<T, std::unique_ptr>
    void Deserialize(RingBuffer& rb, T& t) 
    {
        static_assert(std::is_default_constructible_v<typename T::element_type>);
        if (t.get())
        {
            Deserialize(rb, *t);
        }
        else 
        {
            t = std::make_shared<typename T::element_type>();
            Deserialize(rb, t);
        }
    }

    // for reflected class
    template<ReflectedClass T>
        requires (!CustomBinarytSerializable<T>)
    void Serialize(RingBuffer& rb, const T& t)
    {
        using class_def = class_defination<T>;

        constexpr bool has_base_class = !std::is_same_v<typename class_def::BaseType, void>;

        if constexpr (has_base_class)
        {
            Serialize<typename class_def::BaseType>(rb, t);
        }

        std::apply(
            [&](auto&&... field_def) {
                ([&]() {
                    if constexpr (field_def.Serializable)
                    {
                        Serialize(rb, GetMember(t, field_def));
                    }
                    }(), ...);
            },
            class_def::FieldDefs
        );

        if constexpr(IPostSerialized<T>)
        {
            t.PostSerialized();
        }
    }

    template<ReflectedClass T>
        requires (!CustomBinarytSerializable<T>)
    void Deserialize(RingBuffer& rb, T& t)
    {
        using class_def = class_defination<T>;

        // handle the base type members first
        constexpr bool has_base_class = !std::is_same_v<typename class_def::BaseType, void>;
        if constexpr (has_base_class)
        {
            Deserialize<typename class_def::BaseType>(rb, t);
        }

        // then the members of T
        std::apply(
            [&](auto&&... field_def) {
                ([&]() {
                    if constexpr (field_def.Serializable)
                    {
                        Deserialize(rb, GetMember(t, field_def));
                    }
                }(), ...);
            },
            class_def::FieldDefs
        );

        if constexpr (IPostDeserialized<T>) 
        {
            t.PostDeserialized();
        }
    }
}

namespace MRenderer::JsonSerialization 
{
    using nlohmann::json;

    template<ReflectedClass T>
    void Serialize(json& data, const T& t);

    template<ReflectedClass T>
    void Deserialize(json& data, T& t);

    // for basic type and std::string
    template<typename T>
        requires ((std::is_arithmetic_v<T> || std::is_same_v<T, std::string>) && !ReflectedEnum<T>)
    void Serialize(json& data, const T& t)
    {
        data = t;
    }

    template<typename T>
        requires ((std::is_arithmetic_v<T> || std::is_same_v<T, std::string>) && !ReflectedEnum<T>)
    void Deserialize(json& data, T& t)
    {
        t = data.get<T>();
    }

    // for reflected enum
    template<ReflectedEnum T>
    void Serialize(json& data, const T& t)
    {
        Serialize(data, static_cast<const uint32&>(t));
    }

    template<ReflectedEnum T>
    void Deserialize(json& data, T& t)
    {
        uint32 val;
        Deserialize(data, val);
        t = static_cast<T>(val);
    }

    // for custom serialization
    template<CustomJsonSerializable T>
    void Serialize(json& data, const T& t)
    {
        T::JsonSerialize(data, t);
    }

    template<CustomJsonSerializable T>
    void Deserialize(json& data, T& t)
    {
        T::JsonDeserialize(data, t);
    }

    // for map and unordered_map
    template<typename T>
        requires (is_specialization_v<T, std::map> || is_specialization_v<T, std::unordered_map>)
    void Serialize(json& data, const T& t)
    {
        // do out own serialization if value type is not basic_type or std::string
        if constexpr (!std::is_arithmetic_v<typename T::mapped_type> || !is_specialization_v<typename T::mapped_type, std::basic_string>)
        {
            static_assert(std::is_arithmetic_v<typename T::key_type> || is_specialization_v<typename T::key_type, std::basic_string>);

            data = json::object();
            for (auto& it : t)
            {
                auto json_value = json::object();
                Serialize(json_value, it.second);
                data[it.first] = json_value;
            }
        }
        // otherwise, use the default serialization provided by nlohmann::json
        else
        {
            data = t;
        }
    }

    template<typename T>
        requires (is_specialization_v<T, std::map> || is_specialization_v<T, std::unordered_map>)
    void Deserialize(json& data, T& t)
    {
        if constexpr (!std::is_arithmetic_v<typename T::mapped_type> || !is_specialization_v<typename T::mapped_type, std::basic_string>)
        {
            t.clear();
            for (auto& [key, value] : data.items())
            {
                Deserialize(value, t[key]);
            }
        }
        else
        {
            t = data;
        }
    }

    // for smart pointer
    template<typename T>
        requires is_specialization_v<T, std::unique_ptr> || is_specialization_v<T, std::shared_ptr>
    void Serialize(json& data, T& t)
    {
        if (t.get())
        {
            Serialize(data, *t);
        }
        else
        {
            data = json::object();
        }
    }

    template<typename T>
        requires is_specialization_v<T, std::unique_ptr> || is_specialization_v<T, std::shared_ptr>
    void Deserialize(json& data, T& t)
    {
        static_assert(std::is_default_constructible_v<typename T::element_type>);

        if (t.get())
        {
            Deserialize(data, *t);
        }
        else
        {
            t.reset(new typename T::element_type());
            Deserialize(data, t);
        }
    }

    // for vector
    template<typename T>
        requires is_specialization_v<T, std::vector>
    void Serialize(json& data, const T& t)
    {
        // do out own serialization if value type is not basic_type or std::string
        if constexpr (!std::is_arithmetic_v<typename T::value_type> || !is_specialization_v<typename T::value_type, std::basic_string>)
        {
            data = json::array();
            for (auto& it : t)
            {
                auto obj_json = json::object();
                Serialize(obj_json, it);
                data.push_back(obj_json);
            }
        }
        // otherwise, use the default serialization provided by nlohmann::json
        else
        {
            data = t;
        }
    }

    template<typename T>
        requires is_specialization_v<T, std::vector>
    void Deserialize(json& data, T& t)
    {
        if constexpr (!std::is_arithmetic_v<typename T::value_type> || !is_specialization_v<typename T::value_type, std::basic_string>)
        {
            t.resize(data.size());
            for (uint32 i = 0; i < data.size(); i++)
            {
                Deserialize(data[i], t[i]);
            }
        }
        else
        {
            t = data;
        }
    }

    // for std::array
    template<typename T>
        requires is_array_v<T>
    void Serialize(json& data, const T& t)
    {
        if constexpr (ReflectedClass<typename T::value_type>)
        {
            data = json::array();
            for (uint32 i = 0; i < t.size(); i++)
            {
                data.push_back(json::object());
                Serialize(data[i], t);
            }
        }
        else
        {
            data = t;
        }
    }

    template<typename T>
        requires is_array_v<T>
    void Deserialize(json& data, T& t)
    {
        if constexpr (ReflectedClass<typename T::value_type>)
        {
            for (uint32 i = 0; i < t.size(); i++)
            {
                Deserialize(data[i], t[i]);
            }
        }
        else
        {
            t = data;
        }
    }

    // for reflected class
    template<ReflectedClass T>
    void Serialize(json& data, const T& t)
    {
        using class_def = class_defination<T>;
        using BaseType = typename class_def::BaseType;

        // handle the base type members first
        constexpr bool has_base_class = !std::is_same_v<BaseType , void>;
        if constexpr (has_base_class)
        {
            using base_class_def = class_defination<BaseType>;
            auto& base_class_json = data[FormatBaseClassString(base_class_def::Name)];

            Serialize<typename class_def::BaseType>(base_class_json, t);
        }

        // serialize members in the object @t
        std::apply(
            [&](auto&... field_def) {
                (([&]() {
                    if constexpr (field_def.Serializable)
                    {
                        Serialize(data[field_def.Name], GetMember(t, field_def));
                    }
                    }()), ...);
            },
            class_def::FieldDefs
        );

        if constexpr (IPostSerialized<T>) 
        {
            t.PostSerialized();
        }
    }

    template<ReflectedClass T>
    void Deserialize(json& data, T& t)
    {
        using class_def = class_defination<T>;
        using BaseType = typename class_def::BaseType;

        // handle the base type members first
        constexpr bool has_base_class = !std::is_same_v<BaseType, void>;
        if constexpr (has_base_class)
        {
            using base_class_def = class_defination<BaseType>;
            auto& base_class_json = data[FormatBaseClassString(base_class_def::Name)];

            Deserialize<typename class_def::BaseType>(base_class_json, t);
        }

        std::apply(
            [&](auto&... field_def) {
                (([&]() {
                    if constexpr (field_def.Serializable)
                    {
                        auto it = data.find(field_def.Name);
                        if (it != data.end())
                        {
                            Deserialize(*it, GetMember(t, field_def));
                        }
                    }

                    }()), ...);
            },
            class_def::FieldDefs
        );

        if constexpr (IPostDeserialized<T>)
        {
            t.PostDeserialized();
        }
    }
}


namespace MRenderer
{
    class BinarySerializer
    {
    public:
        BinarySerializer() = default;
        BinarySerializer(std::string_view filepath)
        {
            ASSERT(LoadFile(filepath));
        }

        bool LoadFile(std::string_view filepath)
        {
            auto file = MRenderer::LoadFile(filepath);
            ASSERT(file.has_value());
            std::ifstream& in = file.value();

            in.seekg(0, std::ios::end);
            std::size_t file_size = in.tellg();
            in.seekg(0, std::ios::beg);

            if (file_size == 0)
            {
                Log("Asset Corrupted");
                return false;
            }

            std::vector<uint8> buffer(file_size);
            in.read(reinterpret_cast<char*>(buffer.data()), file_size);

            mBuffer.Write(buffer.data(), static_cast<uint32>(file_size));
            return true;
        }

        template<typename T>
        void LoadObject(const T& obj)
        {
            BinarySerialization::Serialize(mBuffer, obj);
        }

        bool DumpFile(std::string_view repo_path)
        {
            ASSERT(!repo_path.empty());
            using std::filesystem::path;

            path folder_path = path(repo_path).parent_path();
            ASSERT(std::filesystem::is_directory(folder_path) || std::filesystem::create_directories(folder_path));

            std::ofstream file;
            file.open(repo_path.data(), std::ios::out | std::ios::binary);

            if (!file.is_open())
            {
                Log("Failed To Create File At ", repo_path);
                return false;
            }

            std::vector<uint8> data = mBuffer.Dump();
            file.write(reinterpret_cast<const char*>(data.data()), data.size());

            mBuffer.Reset();
            return true;
        }

        template<typename T>
        void DumpObject(T& out_obj)
        {
            BinarySerialization::Deserialize(mBuffer, out_obj);
        }

        inline void Reset()
        {
            mBuffer.Reset();
        }

        uint32 Size() const
        {
            return mBuffer.Occupied();
        }

        inline const uint8* RawData() const
        {
            return mBuffer.Data();
        }

    protected:
        RingBuffer mBuffer;
    };
}