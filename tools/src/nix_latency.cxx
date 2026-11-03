#include <nix/nix.hxx>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <pthread.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <unistd.h>

namespace {

constexpr std::size_t minimumPayloadSize = 8U;
constexpr std::size_t maximumPayloadSize = 8U * 1024U * 1024U;
constexpr std::size_t defaultIterations = 100U;
constexpr std::size_t benchmarkStackSize = 64U * 1024U * 1024U;

struct Options {
    std::size_t iterations = defaultIterations;
    std::size_t minimumSize = minimumPayloadSize;
    std::size_t maximumSize = maximumPayloadSize;
    bool showHelp = false;
};

struct BenchmarkContext {
    const Options& options;
    std::exception_ptr error;
};

std::size_t parseNumber(std::string_view value, std::string_view optionName) {
    std::size_t result = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size()) {
        throw std::invalid_argument("invalid value for " + std::string(optionName));
    }
    return result;
}

void printUsage(std::ostream& output) {
    output << "Usage: nix-latency [--iterations COUNT] [--min-size BYTES] [--max-size BYTES]\n"
        << "Measure local publish-to-receive latency between two Nix nodes.\n"
        << "Payload sizes must be powers of two from 8 bytes through 8 MiB.\n"
        << "Defaults: 100 iterations per size, sweeping 8 bytes through 8 MiB.\n";
}

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--help" || argument == "-h") {
            options.showHelp = true;
            return options;
        }
        if (index + 1 >= argc) {
            throw std::invalid_argument("missing value for " + std::string(argument));
        }

        const std::size_t value = parseNumber(argv[++index], argument);
        if (argument == "--iterations") {
            options.iterations = value;
        } else if (argument == "--min-size") {
            options.minimumSize = value;
        } else if (argument == "--max-size") {
            options.maximumSize = value;
        } else {
            throw std::invalid_argument("unknown option: " + std::string(argument));
        }
    }

    const auto validSize = [](std::size_t size) {
        return size >= minimumPayloadSize && size <= maximumPayloadSize && (size & (size - 1U)) == 0U;
    };
    if (options.iterations == 0U) {
        throw std::invalid_argument("--iterations must be greater than zero");
    }
    if (!validSize(options.minimumSize) || !validSize(options.maximumSize)
        || options.minimumSize > options.maximumSize) {
        throw std::invalid_argument("size bounds must be ordered powers of two from 8 through 8388608 bytes");
    }
    return options;
}

template <std::size_t PayloadSize>
void measureLatency(const Options& options, nix::Node& publisherNode, nix::Node& subscriberNode) {
    constexpr std::uint64_t channelCapacity = 2U;
    const std::string topic = "/nix_latency_" + std::to_string(getpid()) + "_" + std::to_string(PayloadSize);
    auto publisher = publisherNode.createPublisher<std::array<std::uint8_t, PayloadSize>, channelCapacity>(topic);
    auto subscriber = subscriberNode.createSubscriber<std::array<std::uint8_t, PayloadSize>, channelCapacity>(topic);
    auto payload = std::make_unique<std::array<std::uint8_t, PayloadSize>>();
    payload->fill(static_cast<std::uint8_t>(PayloadSize & 0xffU));

    auto minimumLatency = std::chrono::steady_clock::duration::max();
    std::chrono::steady_clock::duration maximumLatency{};
    std::chrono::steady_clock::duration totalLatency{};
    for (std::size_t iteration = 0; iteration < options.iterations; ++iteration) {
        const auto started = std::chrono::steady_clock::now();
        if (!publisher->publish(*payload)) {
            throw std::runtime_error("publish failed during latency measurement");
        }
        const auto received = subscriber->receive();
        const auto finished = std::chrono::steady_clock::now();
        if (!received.has_value() || received->front() != payload->front() || received->back() != payload->back()) {
            throw std::runtime_error("received payload did not match the published data");
        }
        const auto latency = finished - started;
        minimumLatency = std::min(minimumLatency, latency);
        maximumLatency = std::max(maximumLatency, latency);
        totalLatency += latency;
    }

    const double minimumMicroseconds = std::chrono::duration<double, std::micro>(minimumLatency).count();
    const double maximumMicroseconds = std::chrono::duration<double, std::micro>(maximumLatency).count();
    const double averageMicroseconds = std::chrono::duration<double, std::micro>(totalLatency).count()
        / static_cast<double>(options.iterations);
    std::cout << PayloadSize << ',' << options.iterations << ',' << minimumMicroseconds << ','
        << maximumMicroseconds << ',' << averageMicroseconds << '\n';
}

template <std::size_t... Exponents>
void runLatencySweep(const Options& options, nix::Node& publisherNode, nix::Node& subscriberNode,
    std::index_sequence<Exponents...>) {
    ([&] {
        constexpr std::size_t payloadSize = minimumPayloadSize << Exponents;
        if (payloadSize >= options.minimumSize && payloadSize <= options.maximumSize) {
            measureLatency<payloadSize>(options, publisherNode, subscriberNode);
        }
    }(), ...);
}

void* runBenchmark(void* rawContext) noexcept {
    auto& context = *static_cast<BenchmarkContext*>(rawContext);
    try {
        const std::string processId = std::to_string(getpid());
        nix::Node publisherNode("nix_latency_publisher_" + processId);
        nix::Node subscriberNode("nix_latency_subscriber_" + processId);
        std::cout << std::fixed << std::setprecision(3)
            << "payload_bytes,iterations,min_round_trip_us,max_round_trip_us,average_round_trip_us\n";
        runLatencySweep(context.options, publisherNode, subscriberNode, std::make_index_sequence<21>{});
    } catch (...) {
        context.error = std::current_exception();
    }
    return nullptr;
}

void runOnLargeStack(BenchmarkContext& context) {
    pthread_attr_t attributes{};
    int status = pthread_attr_init(&attributes);
    if (status != 0) {
        throw std::system_error(status, std::generic_category(), "pthread_attr_init failed");
    }

    status = pthread_attr_setstacksize(&attributes, benchmarkStackSize);
    if (status != 0) {
        pthread_attr_destroy(&attributes);
        throw std::system_error(status, std::generic_category(), "pthread_attr_setstacksize failed");
    }

    pthread_t worker{};
    status = pthread_create(&worker, &attributes, runBenchmark, &context);
    pthread_attr_destroy(&attributes);
    if (status != 0) {
        throw std::system_error(status, std::generic_category(), "pthread_create failed");
    }

    status = pthread_join(worker, nullptr);
    if (status != 0) {
        throw std::system_error(status, std::generic_category(), "pthread_join failed");
    }
    if (context.error) {
        std::rethrow_exception(context.error);
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parseOptions(argc, argv);
        if (options.showHelp) {
            printUsage(std::cout);
            return 0;
        }

        BenchmarkContext context{options, {}};
        runOnLargeStack(context);
    } catch (const std::exception& error) {
        std::cerr << "nix-latency: " << error.what() << '\n'
            << "Make sure nix-daemon is running and check the CLI options with --help.\n";
        return 1;
    }
    return 0;
}