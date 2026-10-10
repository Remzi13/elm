#include "core/FileSystem.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

namespace elm {
    namespace {
        namespace fs = std::filesystem;

        auto ToFilesystemPath(const Path& path) -> fs::path
        {
            const auto text = path.string();
            std::u8string utf8;
            utf8.reserve(text.size());
            for (const unsigned char character : text)
                utf8.push_back(static_cast<char8_t>(character));
            return fs::path(utf8);
        }

        auto ToUtf8String(const fs::path& path) -> String
        {
            const auto text = path.u8string();
            return String(reinterpret_cast<const char*>(text.data()), text.size());
        }

        auto ToPath(const fs::path& path) -> Path
        {
            return Path(ToUtf8String(path));
        }

        auto InvalidIteratorError() -> std::error_code
        {
            return std::make_error_code(std::errc::no_such_file_or_directory);
        }
    }

    Path::Path(StringView path)
        : m_path(path)
    {
    }

    Path::Path(const char* path)
        : m_path(path)
    {
    }

    Path::Path(const String& path)
        : m_path(path)
    {
    }

    auto Path::operator=(StringView path) -> Path&
    {
        m_path = path;
        return *this;
    }

    auto Path::operator=(const char* path) -> Path&
    {
        m_path = path;
        return *this;
    }

    auto Path::operator=(const String& path) -> Path&
    {
        m_path = path;
        return *this;
    }

    auto Path::operator/(const Path& other) const -> Path
    {
        return ToPath(ToFilesystemPath(*this) / ToFilesystemPath(other));
    }

    auto Path::operator/(const char* path) const -> Path
    {
        return *this / Path(path);
    }

    auto Path::operator/(StringView path) const -> Path
    {
        return *this / Path(path);
    }

    auto Path::operator/(const String& path) const -> Path
    {
        return *this / Path(path);
    }

    auto Path::operator/=(const Path& other) -> Path&
    {
        auto result = ToFilesystemPath(*this);
        result /= ToFilesystemPath(other);
        m_path = ToUtf8String(result);
        return *this;
    }

    auto Path::operator/=(const char* path) -> Path&
    {
        return *this /= Path(path);
    }

    auto Path::operator/=(StringView path) -> Path&
    {
        return *this /= Path(path);
    }

    auto Path::operator/=(const String& path) -> Path&
    {
        return *this /= Path(path);
    }

    auto Path::operator+=(const Path& other) -> Path&
    {
        auto result = ToFilesystemPath(*this);
        result += ToFilesystemPath(other);
        m_path = ToUtf8String(result);
        return *this;
    }

    auto Path::operator+=(const char* path) -> Path&
    {
        return *this += Path(path);
    }

    auto Path::operator+=(StringView path) -> Path&
    {
        return *this += Path(path);
    }

    auto Path::operator+=(const String& path) -> Path&
    {
        return *this += Path(path);
    }

    auto Path::Empty() const noexcept -> bool
    {
        return m_path.empty();
    }

    auto Path::IsAbsolute() const -> bool
    {
        return ToFilesystemPath(*this).is_absolute();
    }

    auto Path::IsRelative() const -> bool
    {
        return ToFilesystemPath(*this).is_relative();
    }

    auto Path::HsaParentPath() const -> bool
    {
        return ToFilesystemPath(*this).has_parent_path();
    }

    auto Path::HasFilename() const -> bool
    {
        return ToFilesystemPath(*this).has_filename();
    }

    auto Path::Parent() const -> Path
    {
        return ToPath(ToFilesystemPath(*this).parent_path());
    }

    auto Path::Filename() const -> Path
    {
        return ToPath(ToFilesystemPath(*this).filename());
    }

    auto Path::Stem() const -> Path
    {
        return ToPath(ToFilesystemPath(*this).stem());
    }

    auto Path::Extension() const -> Path
    {
        return ToPath(ToFilesystemPath(*this).extension());
    }

    auto Path::LexicallyRelative(const Path& base) const -> Path
    {
        return ToPath(ToFilesystemPath(*this).lexically_relative(ToFilesystemPath(base)));
    }

    auto Path::string() const -> std::string
    {
        return std::string(m_path.begin(), m_path.end());
    }

    auto Path::u8string() const -> std::string
    {
        return string();
    }

    auto Path::generic_string() const -> std::string
    {
        const auto text = ToFilesystemPath(*this).generic_u8string();
        return std::string(reinterpret_cast<const char*>(text.data()), text.size());
    }

    auto Path::c_str() const noexcept -> const char*
    {
        return m_path.c_str();
    }

    auto Path::Clear() noexcept -> void
    {
        m_path.clear();
    }

    auto Path::Compare(const Path& other) const -> int
    {
        return ToFilesystemPath(*this).compare(ToFilesystemPath(other));
    }

    auto Path::operator==(const Path& other) const -> bool
    {
        return ToFilesystemPath(*this) == ToFilesystemPath(other);
    }

    auto Path::operator!=(const Path& other) const -> bool
    {
        return !(*this == other);
    }

    auto Path::operator<(const Path& other) const -> bool
    {
        return Compare(other) < 0;
    }

    auto Path::operator<=(const Path& other) const -> bool
    {
        return Compare(other) <= 0;
    }

    auto Path::operator>(const Path& other) const -> bool
    {
        return Compare(other) > 0;
    }

    auto Path::operator>=(const Path& other) const -> bool
    {
        return Compare(other) >= 0;
    }

    auto FileSystem::CurrentPath(std::error_code& error) -> Path
    {
        return ToPath(fs::current_path(error));
    }

    auto FileSystem::CurrentPath() -> Path
    {
        return ToPath(fs::current_path());
    }

    auto FileSystem::Exists(const Path& path, std::error_code& error) -> bool
    {
        return fs::exists(ToFilesystemPath(path), error);
    }

    auto FileSystem::Exists(const Path& path) -> bool
    {
        return fs::exists(ToFilesystemPath(path));
    }

    auto FileSystem::IsDirectory(const Path& path, std::error_code& error) -> bool
    {
        return fs::is_directory(ToFilesystemPath(path), error);
    }

    auto FileSystem::IsRegularFile(const Path& path, std::error_code& error) -> bool
    {
        return fs::is_regular_file(ToFilesystemPath(path), error);
    }

    auto FileSystem::CreateDirectories(const Path& path, std::error_code& error) -> bool
    {
        return fs::create_directories(ToFilesystemPath(path), error);
    }

    auto FileSystem::OpenForReading(const Path& path, std::ios::openmode mode) -> std::ifstream
    {
        return std::ifstream(ToFilesystemPath(path), mode);
    }

    auto FileSystem::OpenForWriting(const Path& path, std::ios::openmode mode) -> std::ofstream
    {
        return std::ofstream(ToFilesystemPath(path), mode);
    }

    struct Directory::Iterator::Impl {
        explicit Impl(const Path& path, std::error_code& error)
            : iterator(ToFilesystemPath(path), error)
        {
        }

        fs::directory_iterator iterator;
    };

    Directory::Iterator::Iterator() = default;

    Directory::Iterator::Iterator(const Path& path, std::error_code& error)
        : m_impl(std::make_unique<Impl>(path, error))
    {
    }

    Directory::Iterator::~Iterator() = default;
    Directory::Iterator::Iterator(Iterator&&) noexcept = default;
    auto Directory::Iterator::operator=(Iterator&&) noexcept -> Iterator& = default;

    auto Directory::Iterator::IsEnd() const noexcept -> bool
    {
        return !m_impl || m_impl->iterator == fs::directory_iterator{};
    }

    auto Directory::Iterator::CurrentPath() const -> Path
    {
        if (IsEnd())
            throw std::out_of_range("Cannot access the end directory iterator");
        return ToPath(m_impl->iterator->path());
    }

    auto Directory::Iterator::IsDirectory(std::error_code& error) const -> bool
    {
        if (IsEnd()) {
            error = InvalidIteratorError();
            return false;
        }
        return m_impl->iterator->is_directory(error);
    }

    auto Directory::Iterator::IsRegularFile(std::error_code& error) const -> bool
    {
        if (IsEnd()) {
            error = InvalidIteratorError();
            return false;
        }
        return m_impl->iterator->is_regular_file(error);
    }

    auto Directory::Iterator::Extension() const -> Path
    {
        return CurrentPath().Extension();
    }

    auto Directory::Iterator::Next(std::error_code& error) -> void
    {
        if (IsEnd()) {
            error = InvalidIteratorError();
            return;
        }
        m_impl->iterator.increment(error);
    }

    auto Directory::Iterator::operator==(const Iterator& other) const -> bool
    {
        if (IsEnd() && other.IsEnd())
            return true;
        if (IsEnd() || other.IsEnd())
            return false;
        return m_impl->iterator == other.m_impl->iterator;
    }

    auto Directory::Iterator::operator!=(const Iterator& other) const -> bool
    {
        return !(*this == other);
    }

    Directory::Directory(const Path& path)
        : m_path(path)
    {
    }

    auto Directory::Begin(std::error_code& error) const -> Iterator
    {
        return Iterator(m_path, error);
    }
}