import argparse
import struct

from nixipc import Node


parser = argparse.ArgumentParser(description="Display serialized int32 values from a Nix topic")
parser.add_argument("--count", type=int, help="stop after receiving this many values")
parser.add_argument("--topic", default="/nix_example_counter", help="shared topic name")
args = parser.parse_args()

node = Node("python_counter_subscriber")
subscriber = node.create_subscriber(args.topic)
received = 0
while args.count is None or received < args.count:
    schema, encoding, payload = subscriber.receive()
    if schema != "nix.counter.int32/v1" or encoding != "application/x-nix-int32-le":
        raise ValueError(f"unsupported message format: schema={schema}, encoding={encoding}")
    if len(payload) != 4:
        raise ValueError("int32 payload must be exactly four bytes")
    value = struct.unpack("<i", payload)[0]
    print(f"received: {value}", flush=True)
    received += 1