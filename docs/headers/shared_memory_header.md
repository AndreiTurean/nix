# `shared_memory_header.hxx`

Source header: `nix/include/nix/shared_memory_header.hxx`

## Scope

This header defines the versioned metadata stored at the beginning of each Nix shared-memory channel and the compile-time type fingerprint used to validate payload compatibility. It does not create/map the POSIX object or clean up stale segments; those responsibilities belong to `ShmChannel` and the cleanup utility.

## Header Fields

`SharedMemoryHeader` records:

| Field | Purpose |
| --- | --- |
| `magic`, `version`, `headerSize` | Identifies the Nix shared-memory format. |
| `ownerPid`, `ownerStartTime` | Identifies the process that created the channel, including protection against PID reuse. |
| `createdAtSeconds` | Records creation time. |
| `mappedSize` | Records the expected total mapping size. |
| `capacity`, `elementSize`, `typeFingerprint` | Describes the payload ring layout and type. |
| `initializationState` | Distinguishes initializing from ready shared state. |

The associated constants define the current format magic/version and initialization states. Changing shared layout requires considering compatibility and format-version behavior.

## `SharedTypeFingerprint<T>`

The default `SharedTypeFingerprint<T>::value` is a compile-time hash of the compiler-provided type signature, `sizeof(T)`, and `alignof(T)`. This detects many accidental mismatches but is compiler-specific and cannot reflect arbitrary aggregate fields or semantic changes.

Applications may specialize the template with an explicit stable value representing their wire schema version. Change that value whenever the shared representation or meaning changes. `SerializedMessage` provides such a specialization in `serialized_message.hxx`.

## Boundary

`ShmChannel<T, Capacity>` writes and checks this metadata during creation/open. Cleanup code uses header identity and owner fields to reason about stale objects. The header itself provides no synchronization beyond its atomic initialization field and no access to the payload queue.

Related references: [Shared-memory channel](shm_channel.md), [Serialized message format](serialized_message.md).