# `daemon.hxx`

Source header: `nix/include/nix/daemon.hxx`

## Scope

This header defines the Nix daemon control-plane client and the endpoint metadata exchanged through it. It does not define the daemon server and does not transport application payloads.

## Public Types

| Type | Scope |
| --- | --- |
| `EndpointRole` | Labels a registry entry as `Publisher` or `Subscriber`. |
| `EndpointInfo` | Value snapshot containing node name, topic name, type identifier, role, and capacity. |
| `DaemonClient` | Unix-domain-socket connection used for node hello, endpoint registration/unregistration, and discovery. |

## `DaemonClient` Operations

| Member | Behavior |
| --- | --- |
| `DaemonClient(nodeName)` | Connects to the configured daemon socket and identifies the node. |
| `registerEndpoint(...)` | Registers topic, type identifier, role, and capacity. |
| `unregisterEndpoint(...)` | Attempts to remove the matching registration; it is `noexcept`. |
| `discover(topic = {})` | Queries endpoint snapshots, optionally for one topic. |
| `defaultDaemonSocketPath()` | Selects `NIX_DAEMON_SOCKET` when set, otherwise the per-user default path. |

The client serializes request/response exchanges with an internal mutex. It is neither copyable nor movable. A successful discovery result is only a snapshot and can become stale as endpoints start or stop.

## Control/Data Split

`Node` and endpoint classes use `DaemonClient` to register and discover endpoints. Message bytes travel through the topic's shared-memory channel, not through this socket. Once an endpoint exists, payload operations do not require a daemon request per message.

Related references: [Node](node.md), [Shared-memory channel](shm_channel.md).