#pragma once
//#include<string>


namespace MRenderer 
{
    class Console 
    {
    //refer from:https://stackoverflow.com/a/55875595/20196181
    public:
        Console() = delete;

        static bool CreateNewConsole(int16_t minLength);
        static bool RedirectConsoleIO();
        static bool ReleaseConsole();
        static void AdjustConsoleBuffer(int16_t minLength);
        static bool AttachParentConsole(int16_t minLength);
    };

    class Logger 
    {
    public:
        static Logger& Instance();
        static std::string _GenerateFileName();
    private:
        Logger();
        ~Logger();

        bool _RedirectStandardIO();
        bool _ReleaseStandardIO();
        
    public:
        static const std::string LogFileName;
    };
}



