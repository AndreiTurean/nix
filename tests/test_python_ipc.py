import os
import shutil
import socket
import struct
import subprocess
import tempfile
import time
import unittest
import uuid

from nixipc import Node


SCHEMA = "nix.counter.int32/v1"
ENCODING = "application/x-nix-int32-le"
CPP_VALUES = (41, 42, 43)
PYTHON_VALUES = (51, 52, 53)


@unittest.skipUnless(
    os.environ.get("NIX_TEST_DAEMON") and os.environ.get("NIX_TEST_CPP_PEER"),
    "requires NIX_TEST_DAEMON and NIX_TEST_CPP_PEER",
)
class PythonIpcInteropTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp_dir = tempfile.mkdtemp(prefix="nix-python-interop-")
        cls.socket_path = os.path.join(cls.temp_dir, "daemon.sock")
        cls.previous_socket_path = os.environ.get("NIX_DAEMON_SOCKET")
        cls.environment = os.environ.copy()
        cls.environment["NIX_DAEMON_SOCKET"] = cls.socket_path
        os.environ["NIX_DAEMON_SOCKET"] = cls.socket_path
        cls.daemon = subprocess.Popen(
            [os.environ["NIX_TEST_DAEMON"], "--socket", cls.socket_path],
            env=cls.environment,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
        )
        deadline = time.monotonic() + 10
        while True:
            if cls.daemon.poll() is not None:
                stderr = cls.daemon.communicate()[1]
                raise RuntimeError(f"nix-daemon exited during startup: {stderr}")
            if time.monotonic() >= deadline:
                cls.daemon.terminate()
                raise RuntimeError("nix-daemon did not create its socket")
            if os.path.exists(cls.socket_path):
                probe = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                try:
                    probe.connect(cls.socket_path)
                    break
                except OSError:
                    pass
                finally:
                    probe.close()
            time.sleep(0.01)

    @classmethod
    def tearDownClass(cls):
        if cls.daemon.poll() is None:
            cls.daemon.terminate()
            try:
                cls.daemon.wait(timeout=5)
            except subprocess.TimeoutExpired:
                cls.daemon.kill()
                cls.daemon.wait()
        if cls.daemon.stderr is not None:
            cls.daemon.stderr.close()
        if cls.previous_socket_path is None:
            os.environ.pop("NIX_DAEMON_SOCKET", None)
        else:
            os.environ["NIX_DAEMON_SOCKET"] = cls.previous_socket_path
        shutil.rmtree(cls.temp_dir, ignore_errors=True)

    def wait_for_endpoint(self, node, topic, role):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if any(endpoint["role"] == role for endpoint in node.discover(topic)):
                return
            time.sleep(0.01)
        self.fail(f"timed out waiting for {role} on {topic}")

    def start_peer(self, mode):
        topic = f"/nix_python_interop_{uuid.uuid4().hex}"
        self.topic = topic
        return subprocess.Popen(
            [os.environ["NIX_TEST_CPP_PEER"], mode, topic],
            env=self.environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

    def assert_peer_succeeded(self, peer):
        try:
            stdout, stderr = peer.communicate(timeout=15)
        except subprocess.TimeoutExpired:
            peer.kill()
            stdout, stderr = peer.communicate()
            self.fail(f"C++ peer timed out; stdout={stdout!r}, stderr={stderr!r}")
        self.assertEqual(peer.returncode, 0, f"stdout={stdout!r}, stderr={stderr!r}")

    def test_python_publishes_to_cpp_subscriber(self):
        peer = self.start_peer("cpp-subscribe")
        try:
            node = Node("python_interop_publisher")
            publisher = node.create_publisher(self.topic, SCHEMA, ENCODING)
            self.wait_for_endpoint(node, self.topic, "subscriber")
            for value in PYTHON_VALUES:
                self.assertTrue(publisher.publish(struct.pack("<i", value)))
            self.assert_peer_succeeded(peer)
        finally:
            if peer.poll() is None:
                peer.kill()
            peer.communicate()

    def test_cpp_publishes_to_python_subscriber(self):
        peer = self.start_peer("cpp-publish")
        try:
            node = Node("python_interop_subscriber")
            subscriber = node.create_subscriber(self.topic)
            received = [subscriber.receive() for _ in CPP_VALUES]
            self.assertEqual(
                received,
                [(SCHEMA, ENCODING, struct.pack("<i", value)) for value in CPP_VALUES],
            )
            self.assert_peer_succeeded(peer)
        finally:
            if peer.poll() is None:
                peer.kill()
            peer.communicate()


if __name__ == "__main__":
    unittest.main()