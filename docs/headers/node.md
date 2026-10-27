# `node.hxx`

Source header: `nix/include/nix/node.hxx`

## Scope

`nix::Node` is the application-facing context for creating publisher/subscriber endpoints and querying registered endpoint metadata. It associates a node name with a `DaemonClient` connection. It is not a message broker and does not carry payload data.

## Public API

| Member | Purpose |
| --- | --- |
| `Node(nodeName)` | Opens a daemon client connection and identifies this participant by name. |
| `createPublisher<T, Capacity>(topic)` | Creates a typed publisher and registers it with the daemon. |
| `createSubscriber<T, Capacity>(topic)` | Opens a typed subscriber and registers it with the daemon. |
| `discover(topic = {})` | Returns endpoint metadata, optionally filtered by topic. |
| `name()` | Returns the node name as a non-owning `string_view`. |

The factory methods return `unique_ptr` endpoint handles. A failed daemon connection or endpoint setup is surfaced as an exception from construction/factory work.

## Ownership and Lifetime

`Node` stores its name and a `shared_ptr<DaemonClient>`. Each endpoint receives a copy of that shared pointer. Consequently, the daemon connection remains alive while any endpoint created by this node remains alive, even if the `Node` object is destroyed first.

Endpoint objects unregister themselves on destruction. `Node` does not own endpoint objects returned to the caller, nor does it automatically stop or wait for them.

## Boundaries

- The daemon connection is for endpoint registration and discovery.
- Payload traffic uses the endpoint's shared-memory channel.
- `Node` does not maintain a local topic queue or serialize user payloads.
- The template type and capacity used at each endpoint must match the shared channel.

Related references: [Publisher](publisher.md), [Subscriber](subscriber.md), [DaemonClient](daemon.md).