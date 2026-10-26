"""ctypes wrapper for the int32 Nix C ABI bridge."""

import ctypes
import os
from pathlib import Path


def _load_library():
    configured_path = os.environ.get("NIX_PYTHON_LIBRARY")
    candidates = [Path(configured_path)] if configured_path else []
    candidates.append(Path(__file__).resolve().parents[1] / "build/default/nix/lib/libnix_python.so")
    for candidate in candidates:
        if candidate.is_file():
            return ctypes.CDLL(str(candidate))
    raise RuntimeError(
        "Could not find libnix_python.so; build the project or set NIX_PYTHON_LIBRARY"
    )


_library = _load_library()
_library.nix_python_last_error.restype = ctypes.c_char_p
_library.nix_python_int32_publisher_create.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
_library.nix_python_int32_publisher_create.restype = ctypes.c_void_p
_library.nix_python_int32_publish.argtypes = [ctypes.c_void_p, ctypes.c_int32]
_library.nix_python_int32_publish.restype = ctypes.c_int
_library.nix_python_int32_publisher_destroy.argtypes = [ctypes.c_void_p]
_library.nix_python_int32_subscriber_create.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
_library.nix_python_int32_subscriber_create.restype = ctypes.c_void_p
_library.nix_python_int32_receive.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int32)]
_library.nix_python_int32_receive.restype = ctypes.c_int
_library.nix_python_int32_subscriber_destroy.argtypes = [ctypes.c_void_p]

CHANNEL_CAPACITY = 16


def _last_error():
    message = _library.nix_python_last_error()
    return message.decode("utf-8", errors="replace") if message else "unknown native error"


class Int32Publisher:
    def __init__(self, node_name, topic_name):
        self._handle = _library.nix_python_int32_publisher_create(
            node_name.encode(), topic_name.encode()
        )
        if not self._handle:
            raise RuntimeError(_last_error())

    def publish(self, value):
        value = int(value)
        if value < -(2**31) or value > 2**31 - 1:
            raise ValueError("value must fit in std::int32_t")
        result = _library.nix_python_int32_publish(self._handle, value)
        if result < 0:
            raise RuntimeError(_last_error())
        return result == 1

    def close(self):
        if self._handle:
            _library.nix_python_int32_publisher_destroy(self._handle)
            self._handle = None

    def __enter__(self):
        return self

    def __exit__(self, _exception_type, _exception, _traceback):
        self.close()


class Int32Subscriber:
    def __init__(self, node_name, topic_name):
        self._handle = _library.nix_python_int32_subscriber_create(
            node_name.encode(), topic_name.encode()
        )
        if not self._handle:
            raise RuntimeError(_last_error())

    def receive(self):
        value = ctypes.c_int32()
        result = _library.nix_python_int32_receive(self._handle, ctypes.byref(value))
        if result < 0:
            raise RuntimeError(_last_error())
        if result == 0:
            return None
        return value.value

    def close(self):
        if self._handle:
            _library.nix_python_int32_subscriber_destroy(self._handle)
            self._handle = None

    def __enter__(self):
        return self

    def __exit__(self, _exception_type, _exception, _traceback):
        self.close()