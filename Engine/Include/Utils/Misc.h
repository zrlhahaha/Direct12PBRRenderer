#pragma once
#include "Fundation.h"
#include "Constexpr.h"

#include <functional>
#include <random>
#include <sstream>
#include <string>
#include <optional>
#include <filesystem>
#include <cerrno>
#include <fstream>

namespace MRenderer 
{
    struct UUID
    {
        static constexpr uint64 BitMask = 0b000000001000010000100001000000000000; // mask off the "-" character
        static constexpr uint32 StringLength = 36; // 32 char and 4 "-"
    public:
        UUID();
        UUID(const std::string& str);

    public:
        static UUID Generate();

        inline static UUID Empty() 
        {
            return UUID{};
        }

        operator std::string();

    protected:
        char m[32];
    };


    class RingBuffer
    {
        static constexpr uint32 InitialBufferCapacity = 256;

    public:
        
        template<typename T>
        requires std::is_arithmetic_v<T>
        void Write(T t)
        {
            Write(reinterpret_cast<uint8*>(&t), sizeof(T));
        }

        template<typename T>
        requires std::is_arithmetic_v<T>
        T Read() 
        {
            return *reinterpret_cast<const T*>(Read(sizeof(T)));
        }

        inline void Reset() 
        {
            mBegin = mEnd = 0;
            mFull = false;
        }

        inline uint32 Capacity() const
        {
            return mCapacity;
        }

        inline const uint8* Data() const 
        {
            return mBuffer;
        }

        const uint8* Peek(uint32 size);
        const uint8* Read(uint32 size);
        void Write(const uint8* data, uint32 size);


        uint32 Occupied() const;
        uint32 Avaliable() const;

        std::vector<uint8> Dump() const;

    protected:
        void Extend(uint32 required_size);

    protected:
        uint8* mBuffer = nullptr;
        uint32 mCapacity = 0;
        uint32 mBegin = 0;
        uint32 mEnd = 0;
        bool mFull = true;
    };

    std::string ToString(const std::wstring_view& str);
    std::wstring ToWString(const std::string_view& str);
    
    inline uint32 AlignUp(uint32 size, uint32 alignment)
    {
        return (size + alignment - 1) & ~(alignment - 1);
    }

    template<typename... Args>
    class Event 
    {
    public:
        using FunctionType = std::function<void(Args...)>;

        struct Listener 
        {
            uint32 ID;
            FunctionType Func;
        };

    public:
        void AddFunc(FunctionType func)
        {
            mFuncs.push_back(Listener{ mNextID++, func });
        }

        void RemoveFunc(uint32 id)
        {
            std::erase(mFuncs,
                std::remove_if(mFuncs.begin(), mFuncs.end(),
                    [&](const Listener& l)
                    {
                        return l.ID == id;
                    }
                )
            );
        }

        void Broadcast(Args... args)
        {
            for (const auto& l : mFuncs)
            {
                if (l.Func) 
                {
                    l.Func(args...);
                }
            }
        }

    protected:
        std::vector<Listener> mFuncs;
        uint32 mNextID;
    };

    std::optional<std::ifstream> LoadFile(std::string_view relative_path);
}