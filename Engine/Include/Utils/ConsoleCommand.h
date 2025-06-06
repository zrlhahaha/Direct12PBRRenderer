#pragma once
#include <string_view>
#include <vector>

#include "cmdline.h"
#include "Utils/Thread.h"


namespace MRenderer 
{
    class ConsoleCommand 
    {
        friend class CommandExecutor;
    public:
        virtual void Execute() = 0;
        virtual ~ConsoleCommand() {};

    protected:
        cmdline::parser mParser;
    };

    // import obj file to the output directory
    class ImportModelCommand : public ConsoleCommand
    {
    public:
        ImportModelCommand()
        {
            mParser.add<std::string>("file", 'f', "Model File Path", true, "");
            mParser.add<std::string>("output", 'o', "Repository File Path", true, "");
        }

        void Execute() override;
    };

    // create a unit sphere model and save it to the output directory
    class CreateSphereModelCommand : public ConsoleCommand
    {
    public:
        CreateSphereModelCommand()
        {
            mParser.add<std::string>("output", 'o', "Repository File Path", true, "");
        }

        void Execute() override;
    };

    // import skybox to the output directory
    class ImportCubeMapCommand : public ConsoleCommand
    {
    public:
        ImportCubeMapCommand()
        {
            mParser.add<std::string>("folder", 'f', "Cubemap Folder Path", true, "");
            mParser.add<std::string>("output", 'o', "Repository File Path", true, "");
        }

        void Execute() override;
    };

    class GenerateIrradianceMapCommand : public ConsoleCommand
    {
    public:
        GenerateIrradianceMapCommand()
        {
            mParser.add<std::string>("file", 'f', "CubeMap File Path", true, "");
            mParser.add<std::string>("output", 'o', "Output File Path", true, "");
            mParser.add<bool>("debug", 'd', "Use Debug Mode", true, false);
        }

        void Execute() override;
    };

    class CommandExecutor 
    {
    public:
        CommandExecutor() 
        {
            mCommandMap["ImportModel"] = std::make_unique<ImportModelCommand>();
            mCommandMap["ImportCubeMap"] = std::make_unique<ImportCubeMapCommand>();
            mCommandMap["CreateSphereModel"] = std::make_unique<CreateSphereModelCommand>();
            mCommandMap["GenerateIrradianceMap"] = std::make_unique<GenerateIrradianceMapCommand>();
        }

        ~CommandExecutor() = default;

        void ExecuteCommand(std::string_view command, std::string_view args)
        {
            auto it = mCommandMap.find(std::string(command));
            if (it == mCommandMap.end()) 
            {
                Log("Unknown Command ", command);
                return;
            }
            
            auto cmd = it->second.get();
            if (!args.empty()) 
            {
                cmd->mParser.parse(args.data());
            }

            try
            {
                cmd->Execute();
            }
            catch (const std::exception& e)
            {
                Log("A Exception Was Throwed During Command Execution");
                Log(e.what());
                return;
            }
        }

        void StartReceivingCommand();
    protected:
        std::unordered_map<std::string, std::unique_ptr<ConsoleCommand>> mCommandMap;
    };
}