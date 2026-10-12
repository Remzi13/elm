#pragma once

#include <fstream>
#include <memory>
#include <string>
#include <system_error>

#include "core/Std.hpp"

namespace elm {

class Settings;

class Path {
public:
    Path() = default;
    Path(const Path&) = default;
    Path(Path&&) noexcept = default;
    Path& operator=(const Path&) = default;
    Path& operator=(Path&&) noexcept = default;

    Path(StringView path);
    Path(const char* path);
    Path(const String& path);

    Path& operator=(StringView path);
    Path& operator=(const char* path);
    Path& operator=(const String& path);

    [[nodiscard]] Path operator/(const Path& other) const;
    [[nodiscard]] Path operator/(const char* path) const;
    [[nodiscard]] Path operator/(StringView path) const;
    [[nodiscard]] Path operator/(const String& path) const;
    Path& operator/=(const Path& other);
    Path& operator/=(const char* path);
    Path& operator/=(StringView path);
    Path& operator/=(const String& path);

    Path& operator+=(const Path& other);
    Path& operator+=(const char* path);
    Path& operator+=(StringView path);
    Path& operator+=(const String& path);

    [[nodiscard]] bool Empty() const noexcept;
    [[nodiscard]] bool IsAbsolute() const;
    [[nodiscard]] bool IsRelative() const;
    [[nodiscard]] bool HsaParentPath() const;
    [[nodiscard]] bool HasFilename() const;

    [[nodiscard]] Path Parent() const;
    [[nodiscard]] Path Filename() const;
    [[nodiscard]] Path Stem() const;
    [[nodiscard]] Path Extension() const;
    [[nodiscard]] Path LexicallyRelative(const Path& base) const;

    [[nodiscard]] std::string string() const;
    [[nodiscard]] std::string u8string() const;
    [[nodiscard]] std::string generic_string() const;
    [[nodiscard]] const char* c_str() const noexcept;

    void Clear() noexcept;

    [[nodiscard]] int Compare(const Path& other) const;

    [[nodiscard]] bool operator==(const Path& other) const;
    [[nodiscard]] bool operator!=(const Path& other) const;
    [[nodiscard]] bool operator<(const Path& other) const;
    [[nodiscard]] bool operator<=(const Path& other) const;
    [[nodiscard]] bool operator>(const Path& other) const;
    [[nodiscard]] bool operator>=(const Path& other) const;

private:
    String m_path;
};

class FileSystem {
public:
    FileSystem() = delete;

    /// Root that relative engine paths are resolved against; a relative value is taken from the settings file folder.
    static constexpr StringView RootPathSetting = "FileSystem/RootPath";

    static void RegisterSettings(Settings& settings);
    static void ApplySettings(const Settings& settings, const Path& settingsDirectory);

    /// Root for relative engine paths, the current directory until settings are applied.
    [[nodiscard]] static Path RootPath();
    /// Absolute paths are returned unchanged, relative ones are resolved against RootPath().
    [[nodiscard]] static Path Resolve(const Path& path);
    /// The first of `start` and its parents that contains `name`, otherwise an empty path.
    [[nodiscard]] static Path FindInParents(const Path& start, const Path& name);

    [[nodiscard]] static Path CurrentPath(std::error_code& error);
    [[nodiscard]] static Path CurrentPath();
    [[nodiscard]] static bool Exists(const Path& path, std::error_code& error);
    [[nodiscard]] static bool Exists(const Path& path);
    [[nodiscard]] static bool IsDirectory(const Path& path, std::error_code& error);
    [[nodiscard]] static bool IsRegularFile(const Path& path, std::error_code& error);
    static bool CreateDirectories(const Path& path, std::error_code& error);
    [[nodiscard]] static std::ifstream OpenForReading(
        const Path& path, std::ios::openmode mode = std::ios::in);
    [[nodiscard]] static std::ofstream OpenForWriting(
        const Path& path, std::ios::openmode mode = std::ios::out);
};

class Directory {
public:
    class Iterator {
    public:
        Iterator();
        explicit Iterator(const Path& path, std::error_code& error);
        ~Iterator();
        Iterator(Iterator&&) noexcept;
        Iterator& operator=(Iterator&&) noexcept;
        Iterator(const Iterator&) = delete;
        Iterator& operator=(const Iterator&) = delete;

        [[nodiscard]] bool IsEnd() const noexcept;
        [[nodiscard]] Path CurrentPath() const;
        [[nodiscard]] bool IsDirectory(std::error_code& error) const;
        [[nodiscard]] bool IsRegularFile(std::error_code& error) const;
        [[nodiscard]] Path Extension() const;
        void Next(std::error_code& error);

        [[nodiscard]] bool operator==(const Iterator& other) const;
        [[nodiscard]] bool operator!=(const Iterator& other) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

    explicit Directory(const Path& path);
    [[nodiscard]] Iterator Begin(std::error_code& error) const;

private:
    Path m_path;
};

} // namespace elm
