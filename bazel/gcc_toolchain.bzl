"""GCC toolchain configuration for Envoy."""

load("@gcc_toolchain//toolchain:repositories.bzl", "gcc_toolchain_dependencies")
load("@gcc_toolchain//toolchain:defs.bzl", "register_gcc_toolchains")

def gcc_toolchain_setup():
    """Setup GCC toolchain repositories and registration."""
    gcc_toolchain_dependencies()
    register_gcc_toolchains()
