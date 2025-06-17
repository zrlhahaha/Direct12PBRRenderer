#pragma once
#include <iostream>
#include <string>
#include <assert.h>


#include "Plateform/Windows/WindowsUtils.h"



#define ASSERT(Condition) assert(Condition)
#define UNEXPECTED(MSG) assert(false && (MSG))

#define LIKELY [[likely]]
#define UNLIKEYLY [[unlikely]]


namespace MRenderer
{
    typedef std::int32_t int32;
    typedef std::int16_t int16;
    typedef std::int64_t int64;
    typedef std::uint64_t uint64;
    typedef std::uint32_t uint32;
    typedef std::uint16_t uint16;
    typedef std::uint8_t uint8;

    constexpr uint32 FrameResourceCount = 3;
    constexpr uint32 ShaderResourceMaxTexture = 8;
    constexpr uint32 ShaderResourceMaxSampler = 6;
    constexpr uint32 ShaderResourceMaxUAV = 8;
    constexpr uint32 NumCubeMapFaces = 6;

    constexpr const char* ShaderFolderPath = "Shader";
    constexpr const wchar_t* LShaderFolderPath = L"Shader";
    constexpr std::string_view AssetFolderPath = "Asset";


    template<typename T> requires std::is_convertible_v<T, std::wstring>
    void LogImpl(T&& t)
    {
        std::wcout << t;
    }

    template<typename T>
    void LogImpl(T&& t)
    {
        std::cout << t;
    }

    template<typename... Args>
    void Log(Args&&... args)
    {
        (..., 
            [&]() {
                LogImpl(args);
                LogImpl(" ");
            }()
        );
        LogImpl("\n");
    }

    template<typename... Args>
    void Warn(Args&&... args)
    {
        LogImpl("[WARN] ");
        (..., LogImpl(args));
        LogImpl("\n");
    }
}

