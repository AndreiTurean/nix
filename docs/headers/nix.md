# `nix.hxx`

Source header: `nix/include/nix/nix.hxx`

## Scope

`nix.hxx` is the convenience umbrella header for consumers of the Nix C++ library. It includes the library's public headers so an application can include one file to access nodes, typed endpoints, shared-memory transport, daemon metadata, and serialized-message helpers.

It contains no class definitions or runtime behavior of its own. It does not start `nix-daemon`, create a node, or create a topic; those actions remain explicit API calls.

## Included API Areas

| Header | API area |
| --- | --- |
| `spsc_ring_buffer.hxx` | Fixed-size lock-free ring storage primitive |
| `shm_channel.hxx` | Shared-memory channel and synchronization |
| `publisher.hxx` | Typed publishing endpoint |
| `subscriber.hxx` | Typed receiving endpoint |
| `node.hxx` | Endpoint factory and discovery context |
| `daemon.hxx` | Control-plane client and endpoint metadata |
| `serialized_message.hxx` | Portable framed payload envelope |

`shared_memory_header.hxx` is included transitively by transport and message headers. Applications that need only a subset can include the narrower header directly.

## Usage

```cpp
#include <nix/nix.hxx>
```

Choose a narrower include when minimizing transitive dependencies is important. The umbrella header is an include convenience, not a separate abstraction layer.