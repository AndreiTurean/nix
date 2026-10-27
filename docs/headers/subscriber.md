# `subscriber.hxx`

Source header: `nix/include/nix/subscriber.hxx`

## Scope

`nix::Subscriber<T, Capacity>` is a typed consumer endpoint for one existing topic. It opens the topic's shared-memory channel, tracks itself as active, and owns a per-subscriber read cursor into the channel's shared message buffer. Payloads are stored once rather than copied into private backlogs.

## Public API

| Member | Purpose |
| --- | --- |
| `receive()` | Nonblocking read; returns `std::nullopt` if this subscriber has no unread message. |
| `receiveBlocking()` | Waits for an item and returns the next popped message. |
| `empty()` | Reports whether this subscriber has no unread messages. |
| `size()` | Reports this subscriber's number of unread messages. |

`empty()` and `size()` are specific to this subscriber's cursor.

## Delivery Model

Each active subscriber can receive every published message once. Subscribers advance independently, and a slow subscriber does not cause messages to be silently skipped. The shared buffer applies backpressure when any subscriber has `Capacity` unread messages; `publish()` returns `false` until space is available. Up to 64 subscribers can be active on one channel.

## Lifecycle and Constraints

Construction opens an existing channel and allocates a shared cursor. Destruction releases it and unregisters the endpoint. When the last subscriber is removed, pending messages are discarded by the channel.

`T` and `Capacity` must match the publisher's shared-memory layout and type fingerprint. `receiveBlocking()` can wait indefinitely if no message is published; the API currently has no timeout or cancellation parameter.

Related references: [Node](node.md), [Publisher](publisher.md), [Shared-memory channel](shm_channel.md).