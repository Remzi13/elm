#include "core/Log.hpp"

#include "core/FileSystem.hpp"
#include "core/Settings.hpp"

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <string>
#include <ctime>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace elm::log {

    namespace {

        constexpr StringView PreviousFileSuffix = ".prev";
        /// Buffered file lines are written when this much has gathered, even before the frame ends.
        constexpr std::size_t FileBufferSize = 64 * 1024;

        String formatMessage( char const* pMessageFormat, va_list args )
        {
            if ( pMessageFormat == nullptr )
                return {};

            Vector<char> buffer(256);
            while ( true )
            {
                va_list argsCopy;
                va_copy( argsCopy, args );
                int const result = std::vsnprintf( buffer.data(), buffer.size(), pMessageFormat, argsCopy );
                va_end( argsCopy );

                if ( result >= 0 && static_cast<std::size_t>( result ) < buffer.size() )
                    return String( buffer.data(), static_cast<std::size_t>( result ) );

                std::size_t nextSize;
                if ( result >= 0 )
                {
                    nextSize = static_cast<std::size_t>( result ) + 1;
                }
                else
                {
#if defined(_MSC_VER)
                    va_list sizeArgs;
                    va_copy( sizeArgs, args );
                    int const requiredSize = ::_vscprintf( pMessageFormat, sizeArgs );
                    va_end( sizeArgs );
                    if ( requiredSize < 0 )
                        throw std::runtime_error( "Unable to format log message" );
                    nextSize = static_cast<std::size_t>( requiredSize ) + 1;
#else
                    throw std::runtime_error( "Unable to format log message" );
#endif
                }

                if ( nextSize <= buffer.size() || nextSize > buffer.max_size() )
                    throw std::length_error( "Log message is too large to format" );

                buffer.resize( nextSize );
            }
        }

        char const* getSeverityName(Severity severity)
        {
            switch (severity)
            {
            case Severity::Info: return "INFO";
            case Severity::Warning: return "WARNING";
            case Severity::Error: return "ERROR";
            case Severity::FatalError: return "FATAL";
            }
            return "UNKNOWN";
        }

        char const* getCategoryName(Category category)
        {
            switch (category)
            {
            case Category::Render: return "RENDER";
            case Category::Physics: return "PHYSICS";
            case Category::Core: return "CORE";
            case Category::Script: return "SCRIPT";
            case Category::Invalid: return "INVALID";
            }
            return "UNKNOWN";
        }

        std::tm toLocalTime(std::chrono::system_clock::time_point timestamp)
        {
            auto const time = std::chrono::system_clock::to_time_t(timestamp);
            std::tm localTime{};
#if defined(ELM_PLATFORM_WINDOWS)
            localtime_s(&localTime, &time);
#elif defined(ELM_PLATFORM_LINUX)
            localtime_r(&time, &localTime);
#endif
            return localTime;
        }

        /// One message as a line of the console and the log file, without the line break.
        String formatLine(Entry const& entry)
        {
            std::tm const localTime = toLocalTime(entry.timestamp);
            auto const milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                entry.timestamp.time_since_epoch()).count();
            char time[16]{};
            std::snprintf(time, sizeof(time), "%02d:%02d:%02d.%03lld",
                localTime.tm_hour, localTime.tm_min, localTime.tm_sec,
                static_cast<long long>((milliseconds % 1000 + 1000) % 1000));

            String line = time;
            line += " [";
            line += getSeverityName(entry.severity);
            line += "] [";
            line += getCategoryName(entry.category);
            line += "] [";
            line += entry.sourceInfo;
            line += "] ";
            line += entry.message;
            if (!entry.filename.empty())
                line += " (" + entry.filename + ":" + String(std::to_string(entry.lineNumber).c_str()) + ")";
            return line;
        }

        /// The first line of a log file: the date the messages below belong to.
        String formatFileHeader()
        {
            std::tm const localTime = toLocalTime(std::chrono::system_clock::now());
            char header[64]{};
            std::snprintf(header, sizeof(header), "Log started %04d-%02d-%02d %02d:%02d:%02d",
                localTime.tm_year + 1900, localTime.tm_mon + 1, localTime.tm_mday,
                localTime.tm_hour, localTime.tm_min, localTime.tm_sec);
            return header;
        }

#if defined(ELM_LOG_CONSOLE)
        void writeToConsole(Entry const& entry)
        {
            FILE* const output = entry.severity == Severity::Info ? stdout : stderr;
            std::fputs(formatLine(entry).c_str(), output);
            std::fputc('\n', output);
            std::fflush(output);
        }
#endif

        /// The previous run's log next to the new one: Logs/game.log -> Logs/game.prev.log.
        Path previousFileOf(const Path& file)
        {
            Path previous = file.Parent() / file.Stem();
            previous += PreviousFileSuffix;
            previous += file.Extension();
            return previous;
        }

        /// Creates the file's folder and keeps an existing file as the previous log.
        std::ofstream openRotated(const Path& file)
        {
            std::error_code error;
            if (file.HsaParentPath())
                FileSystem::CreateDirectories(file.Parent(), error);
            if (FileSystem::IsRegularFile(file, error))
                FileSystem::Rename(file, previousFileOf(file), error);
            return FileSystem::OpenForWriting(file, std::ios::out | std::ios::trunc);
        }

        class MessageStore
        {
        public:
            MessageStore()
            {
                m_messages.reserve(m_maxMessages);
            }

            ~MessageStore()
            {
                Flush();
            }

            void Add(Entry entry)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_messages.push_back(std::move(entry));
                if (m_messages.size() > m_maxMessages)
                    m_messages.erase(m_messages.begin());
#if defined(ELM_LOG_CONSOLE)
                writeToConsole(m_messages.back());
#endif
                if (m_file.is_open())
                    BufferForFile(m_messages.back());
            }

            /// The kept messages go first: the file starts with what was logged before it was opened.
            bool OpenFile(const Path& file)
            {
                std::ofstream stream = openRotated(file);
                if (!stream)
                    return false;
                std::lock_guard<std::mutex> lock(m_mutex);
                WriteFileBuffer();
                m_file = std::move(stream);
                m_fileBuffer.reserve(FileBufferSize);
                m_file << formatFileHeader() << '\n';
                for (Entry const& entry : m_messages)
                    m_file << formatLine(entry) << '\n';
                m_file.flush();
                return static_cast<bool>(m_file);
            }

            void Flush()
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                WriteFileBuffer();
            }

            void CloseFile()
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                WriteFileBuffer();
                m_file.close();
            }

            const Vector<Entry>& Get() const
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                return m_messages;
            }

            Vector<Entry> Take()
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                Vector<Entry> messages;
                messages.reserve(m_maxMessages);
                messages.swap(m_messages);
                return messages;
            }

            void Clear()
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_messages.clear();
            }

            void SetMaxMessages(std::size_t maxMessages)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto const newMaxMessages = maxMessages == 0 ? 1 : maxMessages;
                m_messages.reserve(newMaxMessages);
                m_maxMessages = newMaxMessages;
                if (m_messages.size() > m_maxMessages)
                {
                    auto const excess = m_messages.size() - m_maxMessages;
                    m_messages.erase(m_messages.begin(), m_messages.begin() + excess);
                }
            }

            std::size_t GetMaxMessages() const
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                return m_maxMessages;
            }

        private:
            /// Lines wait in the buffer: one write per frame or per full buffer instead of one per message.
            /// A warning or an error is written at once with the lines before it, so they survive a crash.
            void BufferForFile(Entry const& entry)
            {
                m_fileBuffer += formatLine(entry);
                m_fileBuffer += '\n';
                if (entry.severity != Severity::Info || m_fileBuffer.size() >= FileBufferSize)
                    WriteFileBuffer();
            }

            void WriteFileBuffer()
            {
                if (m_fileBuffer.empty())
                    return;
                m_file.write(m_fileBuffer.data(), static_cast<std::streamsize>(m_fileBuffer.size()));
                m_file.flush();
                m_fileBuffer.clear();
            }

        private:
            mutable std::mutex m_mutex;
            Vector<Entry> m_messages;
            std::ofstream m_file;
            /// Formatted lines not yet written to m_file.
            String m_fileBuffer;
            std::size_t m_maxMessages{ 5000 };
        };

        MessageStore& messageStore()
        {
            static MessageStore store;
            return store;
        }

    }

    void add( Severity severity, Category category, char const* pSourceInfo, char const* pFilename, int pLineNumber, char const* pMessageFormat, ... )
    {
        va_list args;
        va_start( args, pMessageFormat );
        String message;
        try
        {
            message = formatMessage( pMessageFormat, args );
        }
        catch ( ... )
        {
            va_end( args );
            throw;
        }
        va_end( args );

        Entry entry{
            std::chrono::system_clock::now(),
            severity,
            category,
            pSourceInfo != nullptr ? pSourceInfo : "",
            pFilename != nullptr ? pFilename : "",
            pLineNumber,
            std::move( message )
        };

        messageStore().Add(std::move(entry));
    }

    const Vector<Entry>& getMessages()
    {
        return messageStore().Get();
    }

    Vector<Entry> takeMessages()
    {
        return messageStore().Take();
    }

    void clearMessages()
    {
        messageStore().Clear();
    }

    void setMaxMessages( std::size_t maxMessages )
    {
        messageStore().SetMaxMessages(maxMessages);
    }

    std::size_t getMaxMessages()
    {
        return messageStore().GetMaxMessages();
    }

    bool openFile( const Path& file )
    {
        return messageStore().OpenFile(file);
    }

    void flushFile()
    {
        messageStore().Flush();
    }

    void closeFile()
    {
        messageStore().CloseFile();
    }

    void registerSettings( Settings& settings )
    {
        settings.Register(Settings::Category::Core, FileSetting, "Log File", String{});
    }

    void applySettings( const Settings& settings, const Path& settingsDirectory )
    {
        const Path file{ settings.Get<String>(Settings::Category::Core, FileSetting, String{}) };
        if (file.Empty()) {
            closeFile();
            return;
        }
        const Path resolved = file.IsAbsolute() ? file : settingsDirectory / file;
        if (!openFile(resolved))
            WARNING_MESSAGE(Category::Core, "Log", "Cannot open the log file %s", resolved.string().c_str());
    }
}
