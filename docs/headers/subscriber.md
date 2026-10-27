# `subscriber.hxx`

Source header: `nix/include/nix/subscriber.hxx`

## Scope

`nix::Subscriber<T, Capacity>` is a typed consumer endpoint for one existing topic. It opens the topic's shared-memory channel, tracks itself as active, and exposes access to the shared queue. It does not own a private backlog or a per-subscriber cursor.

## Public API

| Member | Purpose |
| --- | --- |
| `receive()` | Nonblocking pop; returns `std::nullopt` if the shared queue is empty. |
| `receiveBlocking()` | Waits for an item and returns the next popped message. |
| `empty()` | Reports whether the shared queue currently has no messages. |
| `size()` | Reports the current number of queued messages. |

`empty()` and `size()` describe the channel shared by every subscriber, not a subscriber-local count.

## Delivery Model

Subscribers on one topic compete to pop from the same queue. Each published message is consumed by at most one subscriber, depending on which receives it first. To deliver each message independently to multiple consumers, a separate broadcast abstraction would be required; this class does not implement that behavior.

## Lifecycle and Constraints

Construction opens an existing channel and increments the shared active-subscriber count. Destruction decrements it and unregisters the endpoint. When the last subscriber is removed, pending queue entries are discarded by the channel.

`T` and `Capacity` must match the publisher's shared-memory layout and type fingerprint. `receiveBlocking()` can wait indefinitely if no message is published; the API currently has no timeout or cancellation parameter.

Related references: [Node](node.md), [Publisher](publisher.md), [Shared-memory channel](shm_channel.md).