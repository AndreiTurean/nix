# Nix IPC Middleware

A **C++20** inter-process communication middleware for **Linux and QNX**, using POSIX shared memory (`shm_open`, `mmap`) to exchange data between nodes. The C++ middleware is designed for native Linux builds and cross-compilation to QNX targets.

## Architecture & Core Components

- **`SpscRingBuffer`**: A fixed-size, lock-free Single-Producer Single-Consumer ring-buffer implementation using atomic operations with acquire-release semantics and cache-line alignment (`alignas(64)`) to reduce false sharing. These properties describe the ring when used directly, not end-to-end channel operations.
- **`ShmChannel`**: RAII wrapper managing POSIX shared memory segments. Its operations are serialized by a robust, process-shared mutex. Messages are stored once and tracked with independent cursors, so every active subscriber can receive each message. Channel operations are lock-based, not lock-free; `waitPop()` also blocks while that subscriber has no message.
- **`Publisher` & `Subscriber`**: Strongly-typed endpoints bound to named topics.
- **`Node`**: Central factory context managing endpoint creation and system handles.
- **`nix-daemon`**: Required Unix-domain-socket registry for active nodes and their publisher/subscriber endpoints. It logs node lifecycle, registrations, and discovery queries. Message payloads normally travel directly through shared memory; an opt-in bounded debug tap makes recent raw payload samples available to the GUI.
- **`nix-cleanup-shm`**: Validates Nix shared-memory metadata and reports channels whose owner process has exited. It is report-only by default; pass `--apply` to unlink stale channel names.

## Getting Started

The project is split by role: `nix/include/nix` and `nix/src` contain the reusable library, while `nix-daemon/src` contains the daemon and shared-memory cleanup executables. Tests and runnable examples remain in `tests` and `examples`.

For class responsibilities, ownership/lifetime boundaries, and architecture diagrams, see [the library architecture guide](docs/library-architecture.md).

Stale shared-memory channels can be inspected with `build/default/nix/bin/nix-cleanup-shm`. It reports stale Nix-owned segments by default; pass `--apply` to unlink them. The tool validates each segment's versioned header and owner PID/start-time, and ignores unrelated or partially initialized shared-memory objects. Channel mutexes are process-shared and robust, so surviving participants recover a lock held by a process that exited unexpectedly.

Each channel also records a message type fingerprint based on its compiler type signature, size, and alignment. Opening a channel with a different type or incompatible size/alignment is rejected. C++ cannot reflect over aggregate fields to detect every same-size schema edit, so applications that evolve message layouts should specialize `nix::SharedTypeFingerprint<T>` with an explicit schema/version value and change it whenever the wire layout changes. Payloads must be trivially copyable.

Subscriber endpoint lifetimes are tracked in shared memory, so publishing does not contact the daemon. If no subscriber is active, the publish call succeeds but discards the message and any queued backlog instead of retaining stale data for a future subscriber. While subscribers are active, normal bounded-queue backpressure applies; publishers can retry when the ring is full. Endpoint registration and discovery remain daemon control-plane operations.

Delivery is per-subscriber: each active subscriber has an independent cursor over messages in the topic's bounded shared-memory buffer, so every subscriber can receive every published message once. Messages are stored once, not copied into a separate queue per subscriber. Publishing returns `false` when the buffer is full for any active subscriber, applying backpressure rather than overwriting unread messages. Publishing with no active subscribers succeeds and discards the message and any stale backlog. A channel supports up to 64 simultaneous subscribers.

The `examples/versioned_message.cxx` example demonstrates a custom message and a `nix::SharedTypeFingerprint<T>` specialization. Increment the explicit fingerprint whenever fields are added, removed, reordered, or change meaning or representation; producers and subscribers must use the same schema version.

### Prerequisites
- A modern C++20 compiler (GCC/Clang on Linux; `q++` with QNX SDP)
- CMake (3.20+)
- Linux with POSIX shared memory support, or QNX SDP 8.0 with `q++` available in the active shell (AArch64, verified)

### Building & Running Tests
```bash
# Configure the project
cmake --preset default

# Build all targets and tests
cmake --build --preset default

# Run the test suite via CTest
cd build/default && ctest --output-on-failure
```

### QNX Cross-Build

The `qnx-release` preset cross-compiles the C++ middleware, examples, and the QNX Screen/Vulkan daemon GUI from a Linux host for QNX AArch64 using the active QNX SDP environment. The GUI uses Dear ImGui from the `third_party/imgui` submodule. Initialize the submodule and the SDP environment first so the sources, `q++`, Screen, and Vulkan headers and libraries are available, then run:

```bash
git submodule update --init --recursive
cmake --preset qnx-release
cmake --build --preset qnx-release
```

This verifies the QNX build; the binaries must run on a compatible QNX target. The Python extension below is currently supported on Linux only.

The GUI is built as `build/qnx-release-arm64/nix/bin/nix-daemon-gui`. Use its Refresh button to query the daemon and view registered publishers and subscribers.

For payload inspection, open the GUI's Sniffer tab, refresh the endpoint list, select a publisher topic, and choose Start. Stop ends capture; the latest captured sample remains available until another capture starts. The GUI displays the raw bytes in hex without interpreting the C++ payload. Samples are capped at 256 bytes. Publishers only forward bytes while the selected topic is armed by the daemon.

### Counter Example

Start the manager, then run the publisher and subscriber in separate terminals. The subscriber waits until the publisher registers the topic, then displays each received counter value.

```bash
./build/default/nix/bin/nix-daemon
./build/default/nix/bin/counter_publisher
./build/default/nix/bin/counter_subscriber
```

Both examples use the `/nix_example_counter` topic and carry `std::int32_t` values. Stop each process with Ctrl+C.

### Python Nodes

Install the Python extension from the repository with `python3 -m pip install .`. Its CPython binding is implemented in `nix/src/python_interop.cxx`; Python creates `Node`, `Publisher`, and `Subscriber` instances directly. The binding transports a common envelope while Python owns serialization and decoding. Each message carries a schema label, encoding label, and opaque payload bytes. The envelope allows up to 64 schema bytes, 32 encoding bytes, and 63 KiB payload bytes, with a channel capacity of 16.

The Python extension is supported on Linux with CPython 3.10 through 3.14. The package builds a native C++20 extension, so a compatible C++20 compiler and Python development headers are required when installing from source. CI installs the package with the command above and runs import, endpoint, serialization, and bidirectional C++/Python IPC tests across these Python versions.

```bash
python3 examples/python_counter_publisher.py
python3 examples/python_counter_subscriber.py
```

The Python and C++ counter nodes use the same `/nix_example_counter` topic and canonical `int32` wire format (`nix.counter.int32/v1`, `application/x-nix-int32-le`), so they can interoperate in either direction. Python serializes with `struct.pack("<i", value)` and decodes with `struct.unpack("<i", payload)`; other message schemas may use JSON, MessagePack, Protocol Buffers, or another agreed encoding by supplying matching schema and encoding labels.

### Inspect Active Topics

With `nix-daemon` running, `nix-list` displays active topics, endpoint counts, node names, and registered type identifiers. Pass a topic name to filter the output.

```bash
./build/default/nix/bin/nix-list
./build/default/nix/bin/nix-list /nix_example_counter
```

### Measure IPC Latency

With `nix-daemon` running, `nix-latency` measures publish-to-receive round-trip time between two local Nix nodes for payload sizes from 8 bytes through 8 MiB. It prints CSV rows to standard output with the iteration count and minimum, maximum, and average latency for each size. The default is 100 iterations per size. Size bounds must be powers of two in that range.

```bash
./build/default/nix/bin/nix-latency
./build/default/nix/bin/nix-latency --iterations 100 --min-size 1024 --max-size 1048576
```

The compiled schema-fingerprint example can be run with:

```bash
./build/default/nix/bin/versioned_message_example
```
