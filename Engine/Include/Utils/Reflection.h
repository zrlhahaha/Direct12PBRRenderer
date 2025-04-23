#pragma once
#include "Constexpr.h"
#include <xstring>
#include <type_traits>
#include <tuple>
#include <vector>
#include <array>

#include "Utils/Misc.h"

namespace MRenderer
{
    // TODO: runtime reflection: able to retrieve reflection infomation by typeid(T) at runtime.
    //       by implementing this we can serialize and deserialize polymorphic class

    // this class is only used for collect class infomation in macro defination
    template<typename T> struct class_defination_spec;

    // class reflection
    template<typename T>
    struct class_defination
    {
        using specialization = class_defination_spec<T>;
        
        using Type = T;
        using BaseType = specialization::BaseType;

        static constexpr auto FieldDefs = specialization::collect_fields_defination();
        static constexpr uint32 NumFields = std::tuple_size_v<decltype(FieldDefs)>;
        static constexpr std::string_view Name = specialization::Name;

        template<uint32 index>
            requires(index < NumFields)
        static constexpr auto get_member_defination()
        {
            return std::get<index>(FieldDefs);
        }
    };

    template<typename T> struct eval_member;

    template<typename Class, typename Field> struct eval_member <Field Class::*>
    {
        using field_type = Field;
        using class_type = Class;
    };

    template<typename T, bool serializable>
    struct member_defination
    {
        using FieldType = eval_member<T>::field_type;
        using ClassType = eval_member<T>::class_type;

        static constexpr bool IsConst = std::is_const_v<FieldType>;
        static constexpr bool IsPointer = std::is_pointer_v<FieldType>;
        static constexpr bool Serializable = serializable;
        static constexpr uint32 Size = sizeof(FieldType);

        std::string_view Name;
        FieldType ClassType::* MemberPtr;

        constexpr member_defination(std::string_view name, FieldType ClassType::* member_ptr)
        {
            Name = name;
            MemberPtr = member_ptr;
        }
    };

    template<typename T>
    concept ReflectedClass = requires() { class_defination_spec<T>(); };

    template<typename T, bool S>
    auto& GetMember(typename member_defination<T, S>::ClassType& obj, member_defination<T, S> mem_def)
    {
        return obj.*mem_def.MemberPtr;
    }

    template<typename T, bool S>
    const auto& GetMember(const typename member_defination<T, S>::ClassType& obj, member_defination<T, S> mem_def)
    {
        return obj.*mem_def.MemberPtr;
    }


    // enum reflection
    template<typename T>
    struct  enum_defination_spec;

    template<typename T>
    concept ReflectedEnum = requires() { enum_defination_spec<T>(); };

    template<typename T>
    struct enum_defination 
    {
        using Type = T;

        static constexpr auto EnumDefs = enum_defination_spec<T>::collect_enum_defs;
        static constexpr uint32 NumEnums = std::tuple_size_v<decltype(EnumDefs)>;
    };

    struct enum_value_defination 
    {
        std::string_view Name;
        uint32 Value;

        constexpr enum_value_defination(std::string_view name, uint32 value) 
            :Name(name), Value(value)
        {
        }
    };
}

#define BEGIN_REFLECT_CLASS(Class, Base)\
template<>\
struct class_defination_spec<Class>\
{\
    using Type = Class;\
    using BaseType = Base;\
    static constexpr std::string_view Name = STRINGIFY(Class);\
\
    static constexpr auto collect_fields_defination()\
    {\
        return std::tuple(


#define REFLECT_FIELD(Field, Serializable)\
        member_defination<decltype(&Type::Field), Serializable>(STRINGIFY(Field), &Type::Field)\

#define END_REFLECT_CLASS \
        );\
    }\
};

#define BEGIN_REFLEFCT_ENUM(Enum)\
template<>\
struct enum_defination_spec<Enum>\
{\
    using Type = Enum;\
\
    static constexpr auto collect_enum_defs()\
    {\
        return std::array{

#define REFLECT_ENUM_VALUE(Name)\
            enum_value_defination(#Name, Type::Name)

#define END_REFLECT_ENUM\
        };\
    }\
};

