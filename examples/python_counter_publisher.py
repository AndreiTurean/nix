import argparse
import struct
import time

from nixipc import Node


parser = argparse.ArgumentParser(description="Publish a serialized int32 counter through Nix IPC")
parser.add_argument("--count", type=int, help="stop after publishing this many values")
parser.add_argument("--topic", default="/nix_example_counter", help="shared topic name")
args = parser.parse_args()

node = Node("python_counter_publisher")
publisher = node.create_publisher(
    args.topic,
    schema="nix.counter.int32/v1",
    encoding="application/x-nix-int32-le",
)

counter = 0
published = 0
while args.count is None or published < args.count:
    if publisher.publish(struct.pack("<i", counter)):
        print(f"published: {counter}", flush=True)
        counter += 1
        published += 1
        time.sleep(0.25)
    else:
        time.sleep(0.01)