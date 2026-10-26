from setuptools import Extension, find_packages, setup


extension = Extension(
    "nixipc._nixipc",
    sources=[
        "nix/src/python_interop.cxx",
        "nix/src/daemon_client.cxx",
        "nix/src/nix.cxx",
    ],
    include_dirs=["nix/include"],
    language="c++",
    extra_compile_args=["-std=c++20", "-pthread"],
    extra_link_args=["-pthread"],
)

setup(
    name="nix-ipc",
    version="0.1.0",
    description="Python nodes for the Nix shared-memory IPC library",
    package_dir={"": "python"},
    packages=find_packages(where="python"),
    ext_modules=[extension],
    zip_safe=False,
)
