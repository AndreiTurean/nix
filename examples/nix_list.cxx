#include <nix/daemon.hxx>

#include <cstdlib>
#if defined(__GNUG__)
#include <cxxabi.h>
#endif
#include <exception>
#include <iomanip>
#include <iostream>
#include <map>
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
        std::cerr << "Usage: nix-list [TOPIC]\n";
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
            std::cout << "No active topics found.\n";
            return 0;
        }

        std::cout << std::left << std::setw(32) << "TOPIC"
            << std::right << std::setw(12) << "PUBLISHERS"
            << std::setw(14) << "SUBSCRIBERS" << "  TYPE\n";
        for (const auto& [topic, summary] : topics) {
            std::cout << std::left << std::setw(32) << topic
                << std::right << std::setw(12) << summary.publishers.size()
                << std::setw(14) << summary.subscribers.size()
                << "  " << displayTypeName(summary.typeName) << '\n';
            for (const auto& node : summary.publishers) {
                std::cout << "  publisher: " << node << '\n';
            }
            for (const auto& node : summary.subscribers) {
                std::cout << "  subscriber: " << node << '\n';
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "nix-list: " << error.what() << '\n';
        return 1;
    }
    return 0;
}