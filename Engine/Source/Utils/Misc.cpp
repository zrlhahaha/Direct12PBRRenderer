#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#include <codecvt>
#undef _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

#include <locale>
#include <chrono>

#include "Utils/Misc.h"

namespace MRenderer 
{
    UUID::UUID()
    {
        memset(this, 0, sizeof(UUID));
    }

    UUID::UUID(const std::string& str)
    {
        ASSERT(str.size() == StringLength);

        for (uint32 i = 0; i < StringLength; i++)
        {
            if ((1ULL << i) & BitMask)
                continue;

            m[i] = str[i];
        }
    }

    UUID UUID::Generate()
    {
        const char v[] = "0123456789abcdef";
        UUID uuid;
        for (uint32 i = 0; i < 32; i++)
        {
            uuid.m[i] = v[rand() % std::size(v)];
        }

        return uuid;
    }

    UUID::operator std::string()
    {
        std::string str;
        for (uint32 i = 0; i < StringLength; i++)
        {
            if ((1ULL << i) & BitMask)
            {
                str += "-";
            }
            else
            {
                str += m[i];
            }
        }

        return str;
    }

    void RingBuffer::Write(const uint8* data, uint32 size)
    {
        if (Avaliable() < size)
        {
            Extend(size);
        }

        ASSERT(Avaliable() >= size);

        // mEnd-mCapacity is enough for the data
        if (mEnd + size <= mCapacity) 
        {
            memcpy(mBuffer + mEnd, data, size);
        }
        // mEnd-mCapacity is not enough, split it into two
        else 
        {
            memcpy(mBuffer + mEnd, data, mCapacity - mEnd);
            memcpy(mBuffer, data, size - (mCapacity - mEnd));
        }

        mEnd = (mEnd + size) % mCapacity;
        if (mEnd == mBegin)
        {
            mFull = true;
        }
    }

    const uint8* RingBuffer::Read(uint32 size)
    {
        const uint8* ret = Peek(size);

        mBegin = (mBegin + size) % mCapacity;
        mFull = false;
        return ret;
    }

    RingBuffer::~RingBuffer()
    {
        if (mBuffer != nullptr) 
        {
            free(mBuffer);
            mBuffer = nullptr;
        }
    }

    const uint8* RingBuffer::Peek(uint32 size)
    {
        ASSERT(Occupied() >= size);

        const uint8* ret;

        // mBegin-mCapacity is enough for the data
        if (mBegin + size <= mCapacity)
        {
            ret = mBuffer + mBegin;
        }
        // mBegin-mCapacity is not enough, collect two parts into staging vector and return it
        else
        {
            static thread_local std::vector<uint8> staging;
            staging.clear();
            staging.insert(staging.end(), mBuffer + mBegin, mBuffer - (mCapacity + mBegin));
            staging.insert(staging.end(), mBuffer, mBuffer + (size - (mCapacity - mBegin)));

            ret = staging.data();
        }

        return ret;
    }

    uint32 RingBuffer::Occupied() const
    {
        if (mFull)
        {
            return mCapacity;
        }
        else if (mBegin > mEnd)
        {
            return mCapacity - mBegin + mEnd;
        }
        else if (mEnd > mBegin)
        {
            return mEnd - mBegin;
        }
        else if(mBegin == mEnd)
        {
            return 0;
        }
        else 
        {
            UNEXPECTED("Unexpected");
            return 0;
        }
    }

    uint32 RingBuffer::Avaliable() const
    {
        if (mFull)
        {
            return 0;
        }
        else if (mBegin > mEnd)
        {
            return mBegin - mEnd;
        }
        else if (mEnd > mBegin)
        {
            return mCapacity - mEnd + mBegin;
        }
        else if (mBegin == mEnd) 
        {
            return mCapacity;
        }
        else 
        {
            UNEXPECTED("Unexpected");
            return 0;
        }
    }

    std::vector<uint8> RingBuffer::Dump() const
    {
        std::vector<uint8> ret;

        if (mEnd > mBegin) 
        {
            ret.insert(ret.end(), mBuffer + mBegin, mBuffer + mEnd);
        }
        else if ((mBegin < mEnd) || (mFull))
        {
            ret.insert(ret.end(), mBuffer + mBegin, mBuffer + mCapacity);
            ret.insert(ret.end(), mBuffer, mBuffer + mEnd);
        }
        else 
        {
            UNEXPECTED("Unexpected Code");
        }

        ret.shrink_to_fit();

        return ret;
    }

    void RingBuffer::Extend(uint32 required_space)
    {
        uint32 size;
        if (!mBuffer)
        {
            size = InitialBufferCapacity;
        }
        else
        {
            size = mCapacity * 2;
        }
        
        if (Avaliable() + size - mCapacity < required_space)
        {
            uint32 bit = FLS(Occupied() + required_space) + 1;
            if (bit <= sizeof(uint32) * CHAR_BIT) 
            {
                size = 1 << bit;
            }
            else 
            {
                ASSERT(false && "capacity overflow");
            }
        }

        uint8* new_buffer = reinterpret_cast<uint8*>(malloc(size));
        if (mBuffer) 
        {
            memcpy(new_buffer, mBuffer, mCapacity);
            delete[] mBuffer;
        }

        mCapacity = size;
        mBuffer = new_buffer;
        mFull = false;
    }

    std::string ToString(const std::wstring_view& str)
    {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        return converter.to_bytes(str.data());
    }

    std::wstring ToWString(const std::string_view& str)
    {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        return converter.from_bytes(str.data());
    }

    void PrintBytes(const void* data, uint32 size)
    {
        const uint8* p = reinterpret_cast<const uint8*>(data);
        for (uint32 i = 0; i < size; i++)
        {
            std::cout<<(static_cast<uint32>(p[i])) << " ";
        }
        std::cout << std::endl;
    }

    int64 Time()
    {
        static thread_local std::chrono::steady_clock::time_point time_stamp;

        if (time_stamp == std::chrono::steady_clock::time_point())
        {
            time_stamp = std::chrono::high_resolution_clock::now();
            return 0;
        }
        else 
        {
            auto now = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - time_stamp);
            return duration.count();
        }
    }

    std::optional<std::ifstream> ReadFile(std::string_view path, bool binary/*=false*/)
    {
        std::filesystem::path full_path = std::filesystem::absolute(path);

        int flag = std::ios::in;
        if (binary) 
        {
            flag |= std::ios::binary;
        }

        std::ifstream in(full_path, flag);

        if (!in.is_open()) {
            Log("Failed To Open File At ", full_path);

            // translate @errno and print it to the console
            char errMsg[512];
            strerror_s(errMsg, sizeof(errMsg), errno);
            Error(errMsg);

            return std::nullopt;
        }
        else
        {
            return std::move(in);
        }
    }

    std::optional<std::ofstream> WriteFile(std::string_view path, bool binary/*=false*/)
    {
        namespace fs = std::filesystem;

        fs::path folder_path = fs::path(path).parent_path();
        ASSERT(std::filesystem::is_directory(folder_path) || std::filesystem::create_directories(folder_path));

        int flag = std::ios::out;
        if (binary) 
        {
            flag |= std::ios::binary;
        }

        std::ofstream file;
        file.open(path, flag);

        if (!file.is_open())
        {
            // translate @errno and print it to the console
            char errMsg[512];
            strerror_s(errMsg, sizeof(errMsg), errno);
            Error(errMsg);

            return std::nullopt;
        }
        else 
        {
            return std::move(file);
        }
    }
}