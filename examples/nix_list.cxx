#include <nix/daemon.hxx>
#include <nix/logging.hxx>

#include <cstdlib>
#if defined(__GNUG__)
#include <cxxabi.h>
#endif
#include <exception>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct TopicSummary {
    std::string typeName;
    std::vector<std::string> publishers;
    std::vector<std::string> subscribers;
};

std::string displayTypeName(const std::string& typeName) {
#if defined(__GNUG__)
    int status = 0;
    char* demangled = abi::__cxa_demangle(typeName.c_str(), nullptr, nullptr, &status);
    if (status == 0 && demangled != nullptr) {
        const std::string result(demangled);
        std::free(demangled);
        return result;
    }
    std::free(demangled);
#endif
    return typeName;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 2) {
        NIX_ERROR("Usage: nix-list [TOPIC]");
        return 2;
    }

    try {
        const std::string_view topicFilter = argc == 2 ? argv[1] : std::string_view{};
        nix::DaemonClient client("nix-list");
        const auto endpoints = client.discover(topicFilter);
        std::map<std::string, TopicSummary> topics;
        for (const auto& endpoint : endpoints) {
            auto& summary = topics[endpoint.topicName];
            summary.typeName = endpoint.typeName;
            auto& nodes = endpoint.role == nix::EndpointRole::Publisher
                ? summary.publishers
                : summary.subscribers;
            nodes.push_back(endpoint.nodeName);
        }

        if (topics.empty()) {
            NIX_INFO("No active topics found.");
            return 0;
        }

        std::ostringstream header;
        header << std::left << std::setw(32) << "TOPIC"
            << std::right << std::setw(12) << "PUBLISHERS"
            << std::setw(14) << "SUBSCRIBERS" << "  TYPE";
        NIX_INFO("{}", header.str());
        for (const auto& [topic, summary] : topics) {
            std::ostringstream row;
            row << std::left << std::setw(32) << topic
                << std::right << std::setw(12) << summary.publishers.size()
                << std::setw(14) << summary.subscribers.size()
                << "  " << displayTypeName(summary.typeName);
            NIX_INFO("{}", row.str());
            for (const auto& node : summary.publishers) {
                NIX_INFO("  publisher: {}", node);
            }
            for (const auto& node : summary.subscribers) {
                NIX_INFO("  subscriber: {}", node);
            }
        }
    } catch (const std::exception& error) {
        NIX_ERROR("nix-list: {}", error.what());
        return 1;
    }
    return 0;
}