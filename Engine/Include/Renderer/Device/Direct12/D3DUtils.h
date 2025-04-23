#pragma once
#include <string>
#include <comdef.h>
#include <wrl.h>

#include "d3dx12.h"
#include "Fundation.h"

template<typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

class DxException
{
public:
    DxException(HRESULT hr, const std::string& functionName, const std::string& filename, int lineNumber)
        :mErrorCode(hr), mFunctionName(functionName), mFilename(filename), mLineNumber(lineNumber)
    {
        std::cout << ToString() << std::endl;
    };

    std::string ToString()const 
    {
        // Get the string description of the error code.
        _com_error err(mErrorCode);
        std::string msg = err.ErrorMessage();

        return mFunctionName + "\n" + " failed in " + mFilename + "; line " + std::to_string(mLineNumber) + "; \n hr: " + std::to_string(mErrorCode) + " error : " + msg;
    }

    HRESULT mErrorCode = S_OK;
    std::string mFunctionName;
    std::string mFilename;
    int mLineNumber = -1;
};


#define ThrowIfFailed(x)                                              \
{                                                                     \
    HRESULT hr__ = (x);                                               \
    if(FAILED(hr__)) { throw DxException(hr__, #x, __FILE__, __LINE__); } \
}


#define ReleaseCom(ptr) { if(ptr) LIKELY { ptr->Release(); ptr = nullptr; } }