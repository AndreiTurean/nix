# Nix IPC Middleware v0.1.0

**The first release of Nix IPC Middleware** brings typed, shared-memory inter-process communication to C++20 applications on Linux and QNX.

Nix keeps message payloads on a direct shared-memory data path while a lightweight daemon handles node and endpoint discovery. This release also includes Python interoperability on Linux and a verified QNX AArch64 build.

## Highlights

- **Typed C++ publishers and subscribers** exchange trivially copyable messages through POSIX shared memory.
- **Independent subscriber delivery** gives each subscriber its own read cursor. A slow subscriber applies bounded-queue backpressure instead of having unread messages overwritten.
- **Lightweight discovery** through `nix-daemon` tracks active nodes and endpoints without brokering message payloads.
- **Crash-aware channel handling** identifies stale shared-memory channels and abandoned subscriber cursors.
- **Compatibility checks** validate shared-memory metadata and message type fingerprints when endpoints connect.
- **Python interoperability on Linux** lets Python nodes publish and receive framed, serialized payloads alongside C++ nodes.

## Supported Platforms

- **Linux:** native C++ builds and Python support.
- **QNX:** C++ build and runtime verified on **AArch64** with QNX SDP 8.0.

The Python extension is currently supported on Linux only.

## Build from Source

Linux release build:

```bash
cmake --preset release
cmake --build --preset release
```

QNX AArch64 cross-build from a shell initialized with the QNX SDP environment:

```bash
cmake --preset qnx-release
cmake --build --preset qnx-release
```

The QNX preset selects the `gcc_ntoaarch64le` compiler variant and places its artifacts in `build/qnx-release-arm64`.

## Python

Install the Python extension on Linux from the repository root:

```bash
python3 -m pip install .
```

CPython 3.10 through 3.14 are supported.

## Included in This Release

- `nix_core`, the C++20 middleware library
- `nix-daemon`, the node and endpoint registry
- `nix-cleanup-shm`, a stale shared-memory inspection and cleanup tool
- Publisher, subscriber, discovery, and versioned-message examples
- C++ and Python serialization and IPC interoperability tests

## Delivery Model

Nix is designed for live IPC, not durable messaging. Messages are not retained for future subscribers, and unread messages are not guaranteed to be replayed after a process crash. Applications requiring persistence or replay should provide it separately.
