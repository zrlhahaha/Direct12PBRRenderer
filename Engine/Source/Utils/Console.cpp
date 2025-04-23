#include<windows.h>
#include <cstdio>
#include <iostream>
#include <chrono>
#include <sstream>
#include <format>


#include "Utils/Console.h"
#include "Utils/Thread.h"


namespace MRenderer 
{
    bool Console::CreateNewConsole(int16_t minLength)
    {
        bool result = false;

        // Release any current console and redirect IO to NUL
        ReleaseConsole();

        // Attempt to create new console
        if (AllocConsole())
        {
            AdjustConsoleBuffer(minLength);
            result = RedirectConsoleIO();
        }

        return result;
    }

    bool Console::RedirectConsoleIO()
    {
        bool result = true;
        FILE* fp;

        // Redirect STDIN if the console has an input handle
        if (GetStdHandle(STD_INPUT_HANDLE) != INVALID_HANDLE_VALUE)
        {
            if (freopen_s(&fp, "CONIN$", "r", stdin) != 0)
            {
                result = false;
            }
            else
            {
                setvbuf(stdin, NULL, _IONBF, 0);
            }
        }

        // Redirect STDOUT if the console has an output handle
        if (GetStdHandle(STD_OUTPUT_HANDLE) != INVALID_HANDLE_VALUE)
        { 
            if (freopen_s(&fp, "CONOUT$", "w", stdout) != 0) 
            {
                result = false;
            }
            else 
            {
                setvbuf(stdout, NULL, _IONBF, 0);
            }
        }

        // Redirect STDERR if the console has an error handle
        if (GetStdHandle(STD_ERROR_HANDLE) != INVALID_HANDLE_VALUE)
        {
            if (freopen_s(&fp, "CONOUT$", "w", stderr) != 0) 
            {
                result = false;
            }
            else 
            {
                setvbuf(stderr, NULL, _IONBF, 0);
            }
        }

        // Make C++ standard streams point to console as well.
        std::ios::sync_with_stdio(true);

        // Clear the error state for each of the C++ standard streams.
        std::wcout.clear();
        std::cout.clear();
        std::wcerr.clear();
        std::cerr.clear();
        std::wcin.clear();
        std::cin.clear();

        return result;
    }

    bool Console::ReleaseConsole()
    {
        bool result = true;
        FILE* fp;

        // Just to be safe, redirect standard IO to NUL before releasing.

        // Redirect STDIN to NUL
        if (freopen_s(&fp, "NUL:", "r", stdin) != 0)
            result = false;
        else
            setvbuf(stdin, NULL, _IONBF, 0);

        // Redirect STDOUT to NUL
        if (freopen_s(&fp, "NUL:", "w", stdout) != 0)
            result = false;
        else
            setvbuf(stdout, NULL, _IONBF, 0);

        // Redirect STDERR to NUL
        if (freopen_s(&fp, "NUL:", "w", stderr) != 0)
            result = false;
        else
            setvbuf(stderr, NULL, _IONBF, 0);

        // Detach from console
        if (!FreeConsole())
            result = false;

        return result;
    }

    void Console::AdjustConsoleBuffer(int16_t minLength)
    {
        // Set the screen buffer to be big enough to scroll some text
        CONSOLE_SCREEN_BUFFER_INFO conInfo;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &conInfo);
        if (conInfo.dwSize.Y < minLength)
            conInfo.dwSize.Y = minLength;
        SetConsoleScreenBufferSize(GetStdHandle(STD_OUTPUT_HANDLE), conInfo.dwSize);
    }

    bool Console::AttachParentConsole(int16_t minLength)
    {
        bool result = false;

        // Release any current console and redirect IO to NUL
        ReleaseConsole();

        // Attempt to attach to parent process's console
        if (AttachConsole(ATTACH_PARENT_PROCESS))
        {
            AdjustConsoleBuffer(minLength);
            result = RedirectConsoleIO();
        }

        return result;
    }

    Logger::Logger()
    {
        _RedirectStandardIO();
    }

    Logger::~Logger() 
    {
        _ReleaseStandardIO();
    }

    std::string Logger::_GenerateFileName() 
    {
        std::string s1 = std::format("{:%F %T}", std::chrono::system_clock::now());
        return "Log_" + s1;
    }

    Logger& Logger::Instance() 
    {
        static Logger instance;
        return instance;
    }

    bool Logger::_RedirectStandardIO()
    {
        bool result = true;
        FILE* fp;

        if (GetStdHandle(STD_OUTPUT_HANDLE) != INVALID_HANDLE_VALUE) 
        {
            if (freopen_s(&fp, LogFileName.c_str(), "w", stdout) != 0)
            {
                result = false;
            }
            else 
            {
                setvbuf(stdout, NULL, _IONBF, 0);
            }
        }

        if (GetStdHandle(STD_ERROR_HANDLE) != INVALID_HANDLE_VALUE)
        {
            if (freopen_s(&fp, LogFileName.c_str(), "w", stderr) != 0)
            {
                result = false;
            }
            else 
            {
                setvbuf(stderr, NULL, _IONBF, 0);
            }
        }

        std::ios::sync_with_stdio(true);

        std::wcout.clear();
        std::cout.clear();
        std::wcerr.clear();
        std::cerr.clear();

        return result;
    }

    bool Logger::_ReleaseStandardIO() 
    {
        bool result = true;
        FILE* fp;

        if (freopen_s(&fp, "NUL:", "w", stdout) != 0) 
        {
            result = false;
        }
        else 
        {
            setvbuf(stdout, NULL, _IONBF, 0);
        }

        if (freopen_s(&fp, "NUL:", "w", stderr) != 0) 
        {
            result = false;
        }
        else 
        {
            setvbuf(stderr, NULL, _IONBF, 0);
        }

        return result;
    }

    const std::string Logger::LogFileName = Logger::_GenerateFileName();

}