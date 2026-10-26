import argparse
import time

from nix_python import Int32Publisher


parser = argparse.ArgumentParser(description="Publish an int32 counter through Nix IPC")
parser.add_argument("--count", type=int, help="stop after publishing this many values")
args = parser.parse_args()

topic = "/nix_example_counter"
with Int32Publisher("python_counter_publisher", topic) as publisher:
    counter = 0
    published = 0
    while args.count is None or published < args.count:
        if publisher.publish(counter):
            print(f"published: {counter}", flush=True)
            counter += 1
            published += 1
            time.sleep(0.25)
        else:
            time.sleep(0.01)