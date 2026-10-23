load(":defs.bzl", "gcc_unavailable")

licenses(["notice"])  # Apache 2

# Hermetic GCC from toolshed, selected with `--config=gcc` (`--//bazel:use_gcc`).
toolchain(
    name = "linux_x86_64_toolchain",
    exec_compatible_with = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
    target_compatible_with = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
    target_settings = ["//bazel:gcc_requested"],
    toolchain = "@gcc_linux_x86_64//:cc_toolchain",
    toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
)

# `manual`: only analysed via toolchain resolution, never by wildcard builds (eg `//bazel/...`).
gcc_unavailable(
    name = "unavailable",
    tags = ["manual"],
)

# Registered after the real GCC toolchains: when GCC is requested for a target that no GCC
# toolchain supports (eg Linux aarch64, macOS), fail instead of silently falling through to Clang.
# Limited to real OSes, as `use_gcc` propagates to eg wasm builds, which use their own toolchains.
[
    toolchain(
        name = "unavailable_%s_toolchain" % os,
        target_compatible_with = ["@platforms//os:%s" % os],
        target_settings = ["//bazel:gcc_requested"],
        toolchain = ":unavailable",
        toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
    )
    for os in [
        "linux",
        "macos",
        "windows",
    ]
]
