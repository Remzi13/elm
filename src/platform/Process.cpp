#include "platform/Process.hpp"

#include "core/Unexpected.hpp"

#if defined(ELM_PLATFORM_WINDOWS)
#include <windows.h>
#else
#include <spawn.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <string>

#if !defined(ELM_PLATFORM_WINDOWS)
extern char** environ;
#endif

namespace elm::platform {

    namespace {

        constexpr const char* LogSource = "Process";

        auto ProcessError(StringView message) -> std::unexpected<EngineError>
        {
            return MakeUnexpected(ErrorCode::UnknownError, message, log::Category::Core, LogSource);
        }

        bool HasQuote(std::span<const String> arguments)
        {
            return std::any_of(arguments.begin(), arguments.end(), [](const String& argument) {
                return argument.find('"') != String::npos;
            });
        }

#if defined(ELM_PLATFORM_WINDOWS)
        std::wstring Widen(StringView text)
        {
            const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
            std::wstring wide(static_cast<size_t>(length), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
            return wide;
        }

        std::string Narrow(const std::wstring& text)
        {
            const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
            std::string narrow(static_cast<size_t>(length), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), narrow.data(), length, nullptr, nullptr);
            return narrow;
        }
#endif

    } // namespace

#if defined(ELM_PLATFORM_WINDOWS)

    auto CurrentExecutable() -> EngineResult<Path>
    {
        std::wstring file(MAX_PATH, L'\0');
        DWORD length = 0;
        // A longer path does not fit: try again with more room
        while ((length = GetModuleFileNameW(nullptr, file.data(), static_cast<DWORD>(file.size()))) == file.size())
            file.resize(file.size() * 2);
        if (length == 0)
            return ProcessError("Cannot find the running program's file");
        file.resize(length);
        return Path(Narrow(file).c_str());
    }

    auto StartProcess(const Path& executable, std::span<const String> arguments) -> EngineResult<void>
    {
        if (HasQuote(arguments))
            return ProcessError("A program argument may not hold a quote");
        // Every part quoted: paths may hold spaces
        String commandLine = "\"" + String(executable.string().c_str()) + "\"";
        for (const String& argument : arguments)
            commandLine += " \"" + argument + "\"";
        std::wstring command = Widen(commandLine);
        const std::wstring program = Widen(executable.string());

        STARTUPINFOW startup { };
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process { };
        if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process))
            return ProcessError("Cannot start " + String(executable.string().c_str()));
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return { };
    }

#else

    auto CurrentExecutable() -> EngineResult<Path>
    {
        std::string file(4096, '\0');
        const ssize_t length = readlink("/proc/self/exe", file.data(), file.size());
        if (length <= 0)
            return ProcessError("Cannot find the running program's file");
        file.resize(static_cast<size_t>(length));
        return Path(file.c_str());
    }

    auto StartProcess(const Path& executable, std::span<const String> arguments) -> EngineResult<void>
    {
        if (HasQuote(arguments))
            return ProcessError("A program argument may not hold a quote");
        const std::string program = executable.string();
        Vector<char*> argv;
        argv.push_back(const_cast<char*>(program.c_str()));
        for (const String& argument : arguments)
            argv.push_back(const_cast<char*>(argument.c_str()));
        argv.push_back(nullptr);
        pid_t child = 0;
        // posix_spawn takes non-const strings for historical reasons and does not change them
        if (posix_spawn(&child, program.c_str(), nullptr, nullptr, argv.data(), environ) != 0)
            return ProcessError("Cannot start " + String(program.c_str()));
        return { };
    }

#endif

} // namespace elm::platform
