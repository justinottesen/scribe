#pragma once

/**
 * Concrete types built on top of the scribe core:
 *   - Level     — log severity enum
 *   - Message   — payload carrying a Level and a std::format-constructed string
 *   - ConsoleHandler — writes formatted records to stdout
 *   - FileHandler    — writes formatted records to a file (append mode)
 */

#include <cstdio>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <print>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <scribe/core.hpp>

namespace scribe::defaults {

enum class Level : std::uint8_t { Trace, Debug, Info, Warn, Error, Fatal };

/**
 * A Message is a pre-formatted log entry. The text is produced eagerly at the log()
 * call site via std::format, so formatting cost falls on the producer, not the consumer.
 */
struct Message {
    Level       level{Level::Info};
    std::string text;

    Message() = default;

    template <typename... Args>
    Message(Level lvl, std::format_string<Args...> fmt, Args&&... args)
        : level(lvl)
        , text(std::format(fmt, std::forward<Args>(args)...)) {}
};

namespace detail {

constexpr auto levelString(Level level) -> std::string_view {
    switch (level) {
        case Level::Trace: return "TRACE";
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO ";
        case Level::Warn:  return "WARN ";
        case Level::Error: return "ERROR";
        case Level::Fatal: return "FATAL";
    }
    std::unreachable();
}

inline auto formatRecord(const Record<Message>& r) -> std::string {
    return std::format("{:%Y-%m-%d %H:%M:%S} [{}] {}", r.time, levelString(r.payload.level),
                       r.payload.text);
}

}    // namespace detail

/**
 * Writes records to stdout. A static mutex serialises output across all ConsoleHandler
 * instances, preventing interleaved lines when multiple loggers share the console.
 */
class ConsoleHandler {
public:
    static void handle(const Record<Message>& r) {
        auto              line = detail::formatRecord(r);
        static std::mutex s_mutex;
        std::lock_guard   lock(s_mutex);
        std::println("{}", line);
    }
};

/**
 * Writes records to a file opened in append mode. The file is flushed after each
 * record via std::println. Thread safety is not required — the Logger guarantees
 * that handle() is only called from one thread at a time.
 */
class FileHandler {
public:
    explicit FileHandler(const std::filesystem::path& path)
        : m_file(std::fopen(path.c_str(), "a"), &std::fclose) {
        if (!m_file) { throw std::system_error(errno, std::system_category(), path.string()); }
    }

    void handle(const Record<Message>& r) {
        std::println(m_file.get(), "{}", detail::formatRecord(r));
        std::fflush(m_file.get());
    }

private:
    std::unique_ptr<FILE, decltype(&std::fclose)> m_file;
};

}    // namespace scribe::defaults
