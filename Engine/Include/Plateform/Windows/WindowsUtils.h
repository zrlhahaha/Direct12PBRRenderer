#pragma once
#include <string>

namespace MRenderer 
{
    bool core_dump(std::wstring& path, _EXCEPTION_POINTERS* exc_ptr);

    void PlateformInitialize();

    inline void throw_exception(std::string& message) 
    {
        throw std::exception("message");
    }
}