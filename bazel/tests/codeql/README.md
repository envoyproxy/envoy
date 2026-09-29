# Host LLVM CodeQL build

This downstream bzlmod module is the build used by Envoy's CodeQL CI. It is also a reference for
downstreams that need to build Envoy with a host-installed LLVM toolchain and host glibc.

The module registers the LLVM toolchain installed at `/usr/lib/llvm-22` and uses
`envoy_llvm.host(...)` to expose the same installation to Envoy BUILD targets that directly use
LLVM tools and libraries. Run `bazel build //:common` from this directory after installing Clang,
libc++, libc++abi, libclang, and lld 22.
