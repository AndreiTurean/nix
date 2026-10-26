import argparse

from nix_python import Int32Subscriber


parser = argparse.ArgumentParser(description="Display int32 values from a Nix topic")
parser.add_argument("--count", type=int, help="stop after receiving this many values")
args = parser.parse_args()

topic = "/nix_example_counter"
with Int32Subscriber("python_counter_subscriber", topic) as subscriber:
    received = 0
    while args.count is None or received < args.count:
        value = subscriber.receive()
        if value is not None:
            print(f"received: {value}", flush=True)
            received += 1