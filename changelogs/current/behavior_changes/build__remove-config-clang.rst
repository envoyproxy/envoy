Removed the obsolete Clang Bazel configuration. Clang with libc++ is
now the default toolchain and requires no configuration flag. GCC with libstdc++ remains available
via ``--config=gcc``. Other compiler/standard-library combinations require a user-provided toolchain.
The ``--@envoy//bazel:libc++`` and ``--@envoy//bazel:libstdc++`` flags and the ``force_libcpp``
define have been removed; the standard library is now derived from the compiler. The
``//bazel:force_libcpp`` label is retained as an alias for ``//bazel:libc++_enabled``.
Also, removed the non-functional, obsolete ``clang-local`` Bazel configuration. Downstreams that require a
host LLVM toolchain should use the bzlmod example in ``bazel/tests/codeql``; its LLVM version is
automatically detected from ``clang --version``.
