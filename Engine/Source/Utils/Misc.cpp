#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#include <codecvt>
#undef _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

#include <locale>

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
            if ((1 << i) & BitMask)
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
            if ((1 << i) & BitMask)
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

        uint8* new_buffer = new uint8[size];
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

    std::optional<std::ifstream> LoadFile(std::string_view relative_path)
    {
        std::filesystem::path full_path = std::filesystem::absolute(relative_path);

        std::ifstream in(full_path, std::ios::in | std::ios::binary);

        if (!in.is_open()) {
            Log("Failed To Open File At ", full_path);

            char errMsg[256];
            strerror_s(errMsg, sizeof(errMsg), errno);
            Warn(errMsg);

            return std::nullopt;
        }
        else
        {
            return std::move(in);
        }
    }
}