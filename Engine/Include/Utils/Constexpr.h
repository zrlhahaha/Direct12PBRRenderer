#pragma once
#include <string>
#include "Fundation.h"

#define STRINGIFY(x) #x
#define CONCAT(a, b) a##b


namespace MRenderer 
{
    template<size_t N>
    struct getter
    {
        friend auto count(getter<N>);
    };

    template<size_t N>
    struct setter
    {
        friend auto count(getter<N>) {};
    };

    template<typename T, size_t N = 0>
    consteval size_t unique_id()
    {
        constexpr bool occupied = requires(getter<N> t) { count(t); };

        if constexpr (occupied)
        {
            return unique_id<T, N + 1>();
        }
        else
        {
            return setter<N>(), N;
        }
    }

    // find index of the left-most 1 bit in num
    template<std::integral T>
    constexpr inline int FLS(T num)
    {
        int bit_size = sizeof(uint32_t) * CHAR_BIT;
        for (int i = bit_size; i > 0; i--)
        {
            T mask = 1 << (i - 1);
            if (mask & num)
            {
                return i;
            }
        }
        return -1;
    }

    // compile time unique id
    template<typename T, size_t N = 0>
    constexpr size_t unique_id_v = unique_id<T>();

    template<typename T, typename U>
    uint32 MemberAddressOffset(T U::* member_ptr)
    {
        return reinterpret_cast<uintptr_t>(&((U*)(nullptr)->*member_ptr)) - reinterpret_cast<uintptr_t>(nullptr);
    }

    template<typename Type, template<typename...> typename Template>
    struct is_specialization : std::false_type {};

    template<template<typename...> typename Template, typename... Args>
    struct is_specialization<Template<Args...>, Template> : std::true_type {};

    template<typename T, template<typename...> typename Template>
    constexpr bool is_specialization_v = is_specialization<std::remove_cv_t<T>, Template>::value;

    template<typename T>
    struct is_array : std::false_type {};

    template<typename T, std::size_t N>
    struct is_array<std::array<T, N>> : std::true_type {};

    template<typename T>
    constexpr bool is_array_v = is_array<T>::value;



    // get the Nth parameter in the parameter pack
    template<uint32 N, typename Type, typename... Args>
    struct index_parameter_pack
    {
        using type = index_parameter_pack<N - 1, Args...>::type;
    };

    template<typename Type, typename... Args>
    struct index_parameter_pack<0, Type, Args...>
    {
        using type = Type;
    };


    // get the Nth template argument type of template specialiation T
    template<uint32 index, typename T>
    struct index_template_parameter;

    template<uint32 index, template<typename...> typename Template, typename... Args>
    struct index_template_parameter<index, Template<Args...>>
    {
        using type = index_parameter_pack<index, Args...>;
    };

    // compile time string literal
    template<size_t N>
    struct StringLiteral
    {
        constexpr StringLiteral(const char str[N])
        {
            std::copy(str, str + N + 1, value);
        }

        const char value[N];
    };

    // iterate tuple
    template<typename Fn, typename Tuple, size_t... Index>
    void iterate_imp(Fn func, Tuple tuple, std::index_sequence<Index...>)
    {
        (func(std::get<Index>(tuple)), ...);
    }

    template<typename Fn, typename... Args>
    void iterate(Fn fn, std::tuple<Args...> args) 
    {
        iterate_imp(fn, args, std::make_index_sequence<sizeof...(Args)>());
    }

    // aggragate initialize T with an array of arguments
    template<typename T, typename Arg, uint32 Size, size_t... Index>
    auto aggragate_initialize_impl(Arg (&args)[Size], std::index_sequence<Index...>)
    {
        return T{args[Index]...};
    }

    template<typename T, typename Arg, uint32 Size>
    auto aggragate_initialize(Arg (&args)[Size])
    {
        return aggragate_initialize_impl<T>(args, std::make_index_sequence<Size>());
    }
}