#pragma once

#include "core/config.hpp"

#include "core/Std.hpp"

#include <stdint.h>
#include <chrono>

namespace elm {
    class Path;
    class Settings;
} // namespace elm

namespace elm::log {

    /// Log file path, relative to the folder of the settings file; empty writes no file.
    inline constexpr StringView FileSetting = "Log/File";

    enum class Severity {
        Info = 0,
        Warning,
        Error,
        FatalError,
    };

    enum class Category : int8_t {
        Invalid = -1,
        Render,
        Physics,
        Core,
        Script,
    };

    struct Entry {
        std::chrono::system_clock::time_point timestamp;
        Severity severity;
        Category category;
        String sourceInfo;
        String filename;
        int lineNumber;
        String message;
    };

    void add(Severity severity, Category category, char const* pSourceInfo, char const* pFilename, int pLineNumber, char const* pMessageFormat, ...);
    /// A copy of the kept messages: other threads keep adding new ones.
    [[nodiscard]] Vector<Entry> getMessages();
    Vector<Entry> takeMessages();
    void clearMessages();
    void setMaxMessages(std::size_t maxMessages);
    std::size_t getMaxMessages();

    /// Duplicates every message into the file from now on, from any thread. Lines are buffered and written
    /// by flushFile(), when the buffer is full, or at once with a warning or an error. An existing file of
    /// that name is kept as <stem>.prev<ext>; the messages still kept come first.
    /// Returns false when the file cannot be written: the log goes on without it.
    [[nodiscard]] bool openFile(const Path& file);
    /// Writes the buffered lines of the log file; the engine calls it once per frame.
    void flushFile();
    /// Writes the buffered lines and closes the file.
    void closeFile();

    void registerSettings(Settings& settings);
    /// Opens the file FileSetting names, or closes the open one when the setting is empty.
    void applySettings(const Settings& settings, const Path& settingsDirectory);
} // namespace elm::log

#define LOG_MESSAGE(category, source, ...) elm::log::add(elm::log::Severity::Info, category, source, __FILE__, __LINE__, __VA_ARGS__)
#define WARNING_MESSAGE(category, source, ...) elm::log::add(elm::log::Severity::Warning, category, source, __FILE__, __LINE__, __VA_ARGS__)
#define ERROR_MESSAGE(category, source, ...) elm::log::add(elm::log::Severity::Error, category, source, __FILE__, __LINE__, __VA_ARGS__)
#define FATAL_ERROR_MESSAGE(category, source, ...) elm::log::add(elm::log::Severity::FatalError, category, source, __FILE__, __LINE__, __VA_ARGS__)
#define ERORR_MESSAGE(category, source, ...) ERROR_MESSAGE(category, source, __VA_ARGS__)
