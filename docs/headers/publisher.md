# `publisher.hxx`

Source header: `nix/include/nix/publisher.hxx`

## Scope

`nix::Publisher<T, Capacity>` is the typed publishing endpoint for one topic. It delegates message storage and synchronization to `ShmChannel<T, Capacity>` and endpoint metadata lifetime to `DaemonClient`.

It creates the topic's shared-memory object in exclusive creator mode. A second live public `Publisher` for that same topic cannot independently create the same object. The publisher does not serialize arbitrary C++ objects; `T` is copied into shared-memory slots.

## Public API

| Member | Purpose |
| --- | --- |
| `publish(const T&)` | Enqueues one message. Returns `false` when the bounded queue is full. |
| `capacity()` | Returns the compile-time channel capacity. |

When no subscriber is active, publishing succeeds while the channel discards the new message and any stale queued backlog. This is a live-stream behavior, not persistent delivery.

## Delivery Model

The topic has one shared queue. If multiple subscribers are attached, they compete to consume messages; one message is not copied to every subscriber. There is no broadcast mode in this API.

## Constraints and Lifecycle

- `T` must be trivially copyable for shared-memory storage.
- `Capacity` must be a power of two and must agree across matching endpoints.
- `publish` may be called repeatedly, but callers should retry if it returns `false` and they need to preserve that message.
- Destruction unregisters the publisher and destroys its creator-mode channel handle, which unlinks the topic name.

Related references: [Node](node.md), [Shared-memory channel](shm_channel.md), [Subscriber](subscriber.md).