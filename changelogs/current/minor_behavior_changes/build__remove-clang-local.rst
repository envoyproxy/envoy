Removed the non-functional ``--config=clang-local`` Bazel configuration. Downstreams that require a
host LLVM toolchain should use the bzlmod example in ``bazel/tests/codeql``; its LLVM version is
automatically detected from ``clang --version``.
