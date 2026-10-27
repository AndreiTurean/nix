# `serialized_message.hxx`

Source header: `nix/include/nix/serialized_message.hxx`

## Scope

This header defines a fixed-size portable envelope for transporting opaque serialized payloads through the typed shared-memory channel. It is primarily used by the Python binding, where Python selects the schema/encoding and owns payload serialization. Ordinary C++ users can publish their own trivially copyable message type without using this envelope.

## Public Types and Helpers

| Name | Scope |
| --- | --- |
| `SerializedMessage` | Owning, fixed-size, trivially copyable byte frame. |
| `SerializedMessageView` | Non-owning decoded version/schema/encoding/payload view into a frame. |
| `makeSerializedMessage(...)` | Validates metadata and payload bounds and constructs a zero-padded frame. |
| `decodeSerializedMessage(message)` | Validates a frame and returns views into its bytes. |
| `SharedTypeFingerprint<SerializedMessage>` | Explicit fingerprint for the envelope layout. |

## Frame Layout

The frame has a capacity of 64,624 bytes, with a 16-byte header:

| Offset | Field | Format |
| --- | --- | --- |
| 0-3 | Magic | ASCII `NIXM` |
| 4-5 | Version | Unsigned 16-bit big-endian; currently 1 |
| 6-7 | Schema length | Unsigned 16-bit big-endian; 1-64 bytes |
| 8-9 | Encoding length | Unsigned 16-bit big-endian; 1-32 bytes |
| 10-11 | Reserved | Unsigned 16-bit big-endian; required to be zero |
| 12-15 | Payload length | Unsigned 32-bit big-endian; maximum 63 KiB |
| 16 onward | Body | Schema bytes, encoding bytes, payload bytes, then zero padding |

Schema and encoding labels must be printable ASCII. Both helpers reject invalid labels, lengths, magic/version/reserved values, and nonzero padding as applicable. Payload contents remain opaque to this header.

## View Lifetime

`SerializedMessageView` stores `string_view` and `span` references into the source frame. Keep the `SerializedMessage` alive for as long as any returned view is used; copying the view does not copy the underlying bytes.

Related references: [Shared-memory metadata](shared_memory_header.md), [Python IPC guide](../library-architecture.md#serializedmessage-and-serializedmessageview).