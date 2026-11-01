#include <nix/logging.hxx>

#include <cassert>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

int main()
{
    std::ostringstream captured;
    std::streambuf* previousBuffer = std::cerr.rdbuf(captured.rdbuf());

    const int counter = 1;
    for (int repetition = 0; repetition < 2; ++repetition)
        NIX_INFO("Publishing {}", counter);
    LOG_INFO("Escaped {{}} and {}", 2);
    NIX_DEBUG("Escaped braces {{}} without arguments");

    const std::vector<nix::logging::Entry> initialHistory = nix::logging::history();
    assert(initialHistory.size() == 3U);
    assert(initialHistory[0].repetitions == 2U);
    assert(initialHistory[0].line > 0U);

    for (int index = 0; index < 102; ++index)
        NIX_INFO("Unique {}", index);

    const std::vector<nix::logging::Entry> boundedHistory = nix::logging::history();
    assert(boundedHistory.size() == nix::logging::Manager::historyCapacity);
    assert(boundedHistory.front().message == "Unique 2");
    assert(boundedHistory.back().message == "Unique 101");

    std::cerr.rdbuf(previousBuffer);

    const std::string output = captured.str();
    const std::regex logLine(
        R"(^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} \[INFO\] \[test_logging\.cxx:\d+\] Publishing 1$)");
    const std::size_t firstLineEnd = output.find('\n');
    assert(firstLineEnd != std::string::npos);
    assert(std::regex_match(output.substr(0, firstLineEnd), logLine));
    assert(output.find(" [INFO] [test_logging.cxx:") != std::string::npos);
    assert(output.find(" [DEBUG] [test_logging.cxx:") != std::string::npos);
    assert(output.find("Escaped {} and 2\n") != std::string::npos);
    assert(output.find("Escaped braces {} without arguments\n") != std::string::npos);
}