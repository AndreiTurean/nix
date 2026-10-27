# `spsc_ring_buffer.hxx`

Source header: `nix/include/nix/spsc_ring_buffer.hxx`

## Scope

`nix::SpscRingBuffer<T, Capacity>` is a fixed-capacity lock-free ring-storage primitive. It provides slot ordering and atomic head/tail bookkeeping; it does not allocate shared memory, block waiting for data, perform process synchronization, or manage subscribers.

## Public API

| Member | Purpose |
| --- | --- |
| `push(item)` | Adds an item if there is free capacity; returns `false` when full. |
| `pop()` | Removes the oldest item or returns `std::nullopt` when empty. |
| `empty()` | Checks whether head and tail indicate no queued elements. |
| `size()` | Returns the current number of elements. |
| `capacity()` | Returns the compile-time capacity. |

## Constraints

- `Capacity` must be a power of two; indexing uses a bit mask.
- `T` must be trivially copyable.
- The algorithm is strictly single-producer/single-consumer when used directly.
- It is bounded and does not overwrite unread elements when full.

`ShmChannel` embeds this ring in shared memory and guards its operations with a process-shared mutex. That outer synchronization changes the concurrency available through the channel API; it does not change this ring's standalone SPSC contract.

Related references: [Shared-memory channel](shm_channel.md).