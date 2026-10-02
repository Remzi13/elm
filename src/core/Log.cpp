#include "core/Log.hpp"

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace elm::log {

    namespace {

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

#if defined(ELM_LOG_TO_CONSOLE)
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
            case Category::Invalid: return "INVALID";
            }
            return "UNKNOWN";
        }

        void writeToConsole(Entry const& entry)
        {
            auto const time = std::chrono::system_clock::to_time_t(entry.timestamp);
            std::tm localTime{};
#if defined(_WIN32)
            localtime_s(&localTime, &time);
#else
            localtime_r(&time, &localTime);
#endif
            auto const milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                entry.timestamp.time_since_epoch()).count();
            FILE* const output = entry.severity == Severity::Info ? stdout : stderr;
            std::fprintf(output, "%02d:%02d:%02d.%03lld [%s] [%s] [%s] %s",
                localTime.tm_hour, localTime.tm_min, localTime.tm_sec,
                static_cast<long long>((milliseconds % 1000 + 1000) % 1000),
                getSeverityName(entry.severity),
                getCategoryName(entry.category),
                entry.sourceInfo.c_str(),
                entry.message.c_str());
            if (!entry.filename.empty())
                std::fprintf(output, " (%s:%d)", entry.filename.c_str(), entry.lineNumber);
            std::fputc('\n', output);
            std::fflush(output);
        }
#endif

        class MessageStore
        {
        public:
            MessageStore()
            {
                m_messages.reserve(m_maxMessages);
            }

            void Add(Entry entry)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_messages.push_back(std::move(entry));
                if (m_messages.size() > m_maxMessages)
                    m_messages.erase(m_messages.begin());
#if defined(ELM_LOG_TO_CONSOLE)
                writeToConsole(m_messages.back());
#endif
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
            mutable std::mutex m_mutex;
            Vector<Entry> m_messages;
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
}