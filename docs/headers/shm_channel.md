# `shm_channel.hxx`

Source header: `nix/include/nix/shm_channel.hxx`

## Scope

`nix::ShmChannel<T, Capacity>` is the lower-level POSIX shared-memory transport. It creates or opens a named shared-memory object, maps its shared state into the current process, validates layout metadata, and serializes access to its ring buffer with process-shared synchronization.

This is the transport boundary used by `Publisher` and `Subscriber`; it is not the daemon registry and does not implement message schemas beyond the shared type fingerprint.

## Main Types

| Type | Scope |
| --- | --- |
| `ShmMode::Create` | Exclusively creates and initializes the shared object. The creating handle unlinks its name on destruction. |
| `ShmMode::Open` | Opens and validates an existing object without taking ownership of unlinking its name. |
| `ShmChannel<T, Capacity>` | Local mapping and channel operations over one shared state. |

The shared state consists of a `SharedMemoryHeader`, one fixed-capacity array of message slots, a write index, up to 64 per-subscriber read cursors, a robust process-shared mutex, and a process-shared condition variable.

## Operations

| Operation | Behavior |
| --- | --- |
| `push(item)` | Stores one message for all active subscribers; returns `false` if there are no subscribers or any subscriber has reached capacity. |
| `pushIfSubscribed(item)` | Discards pending data and returns success if no subscribers are active; otherwise stores a message for every subscriber or returns `false` if any subscriber is behind by capacity. |
| `pop(id)` | Nonblocking read for the identified subscriber; each subscriber advances independently. |
| `waitPop(id)` | Waits until a message is available for that subscriber, then reads it. |
| `addSubscriber()` / `removeSubscriber(id)` | Allocates/releases a cursor; supports up to 64 simultaneous subscribers. |
| `empty(id)` / `size(id)` / `capacity()` | Inspects one subscriber's unread message count or the fixed capacity. |
| `discardPending()` | Advances all active subscriber cursors to the current write index. |

## Safety and Compatibility

Opening validates the shared-memory magic, format version, ready state, capacity, element size, and `SharedTypeFingerprint<T>`. A mismatch is rejected rather than interpreting incompatible bytes as `T`.

All channel operations are serialized by a robust process-shared mutex. Each message is stored once; independent subscriber cursors ensure every active subscriber can read it once. Publishing is bounded by the slowest subscriber: it returns `false` rather than overwriting unread data when that subscriber reaches capacity. No active subscribers means the subscriber-aware publish path discards data and returns success.

The channel is non-copyable and movable. Each handle owns its local file descriptor and mapping; only the shared state is common across processes. Creator-handle destruction unlinks the POSIX name, while existing mappings remain mapped until their own handles close.

Related references: [SPSC ring buffer](spsc_ring_buffer.md), [shared-memory metadata](shared_memory_header.md), [Publisher](publisher.md), [Subscriber](subscriber.md).