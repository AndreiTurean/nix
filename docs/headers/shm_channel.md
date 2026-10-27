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

The shared state consists of a `SharedMemoryHeader`, `SpscRingBuffer<T, Capacity>`, active subscriber count, robust process-shared mutex, and process-shared condition variable.

## Operations

| Operation | Behavior |
| --- | --- |
| `push(item)` | Pushes directly into the queue; returns `false` if full. |
| `pushIfSubscribed(item)` | Discards pending data and returns success if no subscribers are active; otherwise pushes and signals a waiter. |
| `pop()` | Nonblocking queue pop. |
| `waitPop()` | Waits on the process-shared condition variable until an item is available, then pops. |
| `addSubscriber()` / `removeSubscriber()` | Maintains the shared active count; removing the final subscriber clears pending data. |
| `empty()` / `size()` / `capacity()` | Inspects the shared queue or its fixed capacity. |
| `discardPending()` | Removes all queued items. |

## Safety and Compatibility

Opening validates the shared-memory magic, format version, ready state, capacity, element size, and `SharedTypeFingerprint<T>`. A mismatch is rejected rather than interpreting incompatible bytes as `T`.

The ring buffer's native algorithm is SPSC. `ShmChannel` wraps each ring operation in a robust process-shared mutex, which permits multiple channel handles to use the same queue safely. This queue model still does not replicate messages to subscribers.

The channel is non-copyable and movable. Each handle owns its local file descriptor and mapping; only the shared state is common across processes. Creator-handle destruction unlinks the POSIX name, while existing mappings remain mapped until their own handles close.

Related references: [SPSC ring buffer](spsc_ring_buffer.md), [shared-memory metadata](shared_memory_header.md), [Publisher](publisher.md), [Subscriber](subscriber.md).