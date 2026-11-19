GCC builds (``--config=gcc``) now use toolshed's hermetic GCC 13.4.0 toolchain instead of a GCC
installed on the host or in the build image. GCC uses Envoy's hermetic sysroot (the same as Clang),
static libstdc++, and links with the LLVM toolchain's LLD instead of gold. It supports Linux x86_64
only, and ``--config=gcc`` now fails on other platforms. The GCC build image is no longer used. To
build with a system GCC, set ``--repo_env=BAZEL_GCC_PATH=<prefix>`` to its installation prefix (eg
``/usr``). The Bazel Go rules and the Go SDK are updated (``rules_go`` 0.63.0, Go 1.27.3), which
this toolchain needs to link Go tools.
