GCC builds (``--config=gcc``) now use toolshed's hermetic GCC 13.4.0 toolchain instead of a GCC
installed in the build image. GCC uses Envoy's hermetic sysroot (the same as Clang), static
libstdc++, and links with hermetic LLD instead of gold. It supports Linux x86_64 only, and
``--config=gcc`` now fails on other platforms instead of building with Clang. The GCC build image,
``--config=libstdc++`` and ``BAZEL_USE_LIBSTDCPP`` are removed. To build with a system GCC, set
``gcc_extension.host(path = "...", toolchains = ["gcc_linux_x86_64"])`` from
``@envoy_toolshed//toolchains/gcc:extensions.bzl`` in the root module.
