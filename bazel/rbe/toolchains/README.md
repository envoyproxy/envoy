# Bazel Toolchains

This directory defines execution platforms for Bazel RBE and Docker sandbox builds.

Both Clang and GCC use downloaded toolchains registered in `MODULE.bazel`, not compilers installed
in the worker image. The GCC platform uses the same worker image as Clang. See
[`bazel/README.md`](../../README.md) for compiler selection and host-toolchain overrides.
