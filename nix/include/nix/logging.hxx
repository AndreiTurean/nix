#pragma once

#include <chrono>
#include <cstddef>
#include <ctime>
#include <deque>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nix::logging {

struct Entry {
    std::string timestamp;
    std::string level;
    std::string file;
    std::size_t line;
    std::string message;
    std::size_t repetitions = 1U;
};

class Manager {
public:
    static constexpr std::size_t historyCapacity = 100U;

    static Manager& instance()
    {
        static Manager manager;
        return manager;
    }

    void record(Entry entry)
    {
        const std::lock_guard<std::mutex> lock(_mutex);
        if (!_entries.empty()) {
            Entry& previous = _entries.back();
            if (previous.level == entry.level && previous.file == entry.file &&
                previous.line == entry.line && previous.message == entry.message) {
                ++previous.repetitions;
                previous.timestamp = std::move(entry.timestamp);
                return;
            }
        }

        if (_entries.size() == historyCapacity)
            _entries.pop_front();
        _entries.push_back(entry);
        std::cerr << entry.timestamp << " [" << entry.level << "] [" << entry.file << ':' << entry.line
            << "] " << entry.message << '\n';
    }

    std::vector<Entry> history() const
    {
        const std::lock_guard<std::mutex> lock(_mutex);
        return {_entries.begin(), _entries.end()};
    }

    void clear()
    {
        const std::lock_guard<std::mutex> lock(_mutex);
        _entries.clear();
    }

private:
    mutable std::mutex _mutex;
    std::deque<Entry> _entries;
};

inline std::vector<Entry> history()
{
    return Manager::instance().history();
}

inline void clearHistory()
{
    Manager::instance().clear();
}

inline void appendFormatted(std::ostringstream& output, std::string_view format)
{
    for (std::size_t index = 0; index < format.size(); ++index) {
        if (format[index] == '{' && index + 1U < format.size() && format[index + 1U] == '}')
            throw std::invalid_argument("Not enough arguments for log format string");
        if ((format[index] == '{' || format[index] == '}') &&
            index + 1U < format.size() && format[index + 1U] == format[index]) {
            output << format[index];
            ++index;
        } else {
            output << format[index];
        }
    }
}

template <typename Argument, typename... Arguments>
void appendFormatted(std::ostringstream& output, std::string_view format,
    Argument&& argument, Arguments&&... arguments)
{
    for (std::size_t index = 0; index < format.size(); ++index) {
        if (format[index] == '{' && index + 1U < format.size() && format[index + 1U] == '{') {
            output << '{';
            ++index;
        } else if (format[index] == '}' && index + 1U < format.size() && format[index + 1U] == '}') {
            output << '}';
            ++index;
        } else if (format[index] == '{' && index + 1U < format.size() && format[index + 1U] == '}') {
            output << std::forward<Argument>(argument);
            appendFormatted(output, format.substr(index + 2U),
                std::forward<Arguments>(arguments)...);
            return;
        } else {
            output << format[index];
        }
    }
    throw std::invalid_argument("Too many arguments for log format string");
}

template <typename... Arguments>
void write(std::string_view level, std::string_view sourceFile, std::size_t sourceLine,
    std::string_view format, Arguments&&... arguments)
{
    std::ostringstream message;
    appendFormatted(message, format, std::forward<Arguments>(arguments)...);

    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm localTime{};
    if (localtime_r(&now, &localTime) == nullptr)
        throw std::runtime_error("Unable to format log timestamp");

    char timestamp[20]{};
    if (std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &localTime) == 0U)
        throw std::runtime_error("Unable to format log timestamp");

    const std::size_t fileNameStart = sourceFile.find_last_of("/\\");
    const std::string_view fileName = fileNameStart == std::string_view::npos
        ? sourceFile
        : sourceFile.substr(fileNameStart + 1U);

    Manager::instance().record({timestamp, std::string(level), std::string(fileName),
        sourceLine, message.str(), 1U});
}

} // namespace nix::logging

#define NIX_TRACE(...) ::nix::logging::write("TRACE", __FILE__, static_cast<std::size_t>(__LINE__), __VA_ARGS__)
#define NIX_DEBUG(...) ::nix::logging::write("DEBUG", __FILE__, static_cast<std::size_t>(__LINE__), __VA_ARGS__)
#define NIX_INFO(...) ::nix::logging::write("INFO", __FILE__, static_cast<std::size_t>(__LINE__), __VA_ARGS__)
#define NIX_WARN(...) ::nix::logging::write("WARN", __FILE__, static_cast<std::size_t>(__LINE__), __VA_ARGS__)
#define NIX_WARNING(...) NIX_WARN(__VA_ARGS__)
#define NIX_ERROR(...) ::nix::logging::write("ERROR", __FILE__, static_cast<std::size_t>(__LINE__), __VA_ARGS__)
#define NIX_FATAL(...) ::nix::logging::write("FATAL", __FILE__, static_cast<std::size_t>(__LINE__), __VA_ARGS__)

#ifndef LOG_TRACE
#define LOG_TRACE(...) NIX_TRACE(__VA_ARGS__)
#endif
#ifndef LOG_DEBUG
#define LOG_DEBUG(...) NIX_DEBUG(__VA_ARGS__)
#endif
#ifndef LOG_INFO
#define LOG_INFO(...) NIX_INFO(__VA_ARGS__)
#endif
#ifndef LOG_WARN
#define LOG_WARN(...) NIX_WARN(__VA_ARGS__)
#endif
#ifndef LOG_WARNING
#define LOG_WARNING(...) NIX_WARNING(__VA_ARGS__)
#endif
#ifndef LOG_ERROR
#define LOG_ERROR(...) NIX_ERROR(__VA_ARGS__)
#endif
#ifndef LOG_FATAL
#define LOG_FATAL(...) NIX_FATAL(__VA_ARGS__)
#endif