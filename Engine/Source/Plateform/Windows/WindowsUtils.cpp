#include <Windows.h>
#include <DbgHelp.h>
#include "Plateform/Windows/WindowsUtils.h"

// Ensure that the dbghelp.lib library is linked.
#pragma comment(lib, "dbghelp.lib")
namespace MRenderer
{
    std::string WorkingDirectory() {
        TCHAR buffer[MAX_PATH] = { 0 };
        GetModuleFileName(NULL, buffer, MAX_PATH);
        size_t pos = std::string(buffer).find_last_of("\\/");
        return std::string(buffer).substr(0, pos);
    }

    bool core_dump(std::string& path, _EXCEPTION_POINTERS* exc_ptr) {
        HANDLE hFile = CreateFile(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) {
            return false;
        }

        MINIDUMP_EXCEPTION_INFORMATION dumpInfo;
        dumpInfo.ThreadId = GetCurrentThreadId();
        dumpInfo.ExceptionPointers = exc_ptr; 
        dumpInfo.ClientPointers = FALSE;

        BOOL ret = MiniDumpWriteDump(
            GetCurrentProcess(),
            GetCurrentProcessId(),
            hFile,
            MiniDumpWithFullMemory,
            &dumpInfo,
            nullptr,
            nullptr
        );

        CloseHandle(hFile);

        return ret;
    }

    LONG MRendererExceptionHandler(_EXCEPTION_POINTERS* exc_ptr)
    {
        auto file_path = WorkingDirectory() + "\\core.dmp";
        core_dump(file_path, exc_ptr);
        return 0;
    }

    void PlateformInitialize()
    {
        SetUnhandledExceptionFilter(MRendererExceptionHandler);
    }
}