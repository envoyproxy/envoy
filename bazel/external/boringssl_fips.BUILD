load("@envoy//bazel/external:fips_build.bzl", "boringssl_fips_build_command", "ninja_build_command")
load("@rules_cc//cc:defs.bzl", "cc_library")

licenses(["notice"])  # Apache 2

# BoringSSL build as described in the Security Policy for BoringCrypto module "update stream":
# https://boringssl.googlesource.com/boringssl/+/refs/heads/main/crypto/fipsmodule/FIPS.md#update-stream

SUPPORTED_ARCHES = {
    "x86_64": "amd64",
    "aarch64": "arm64",
}

STDLIBS = [
    "libc++",
    "libstdc++",
]

# marker for dir
filegroup(
    name = "crypto_marker",
    srcs = ["crypto/crypto.cc"],
)

cc_library(
    name = "crypto",
    srcs = [
        "crypto/libcrypto.a",
    ],
    hdrs = glob(["include/openssl/*.h"]),
    includes = ["include"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "ssl",
    srcs = [
        "ssl/libssl.a",
    ],
    hdrs = glob(["include/openssl/*.h"]),
    includes = ["include"],
    visibility = ["//visibility:public"],
    deps = [":crypto"],
)

genrule(
    name = "ninja_bin",
    srcs = [
        "@fips_ninja//:all",
        "@fips_ninja//:configure.py",
    ],
    outs = ["ninja"],
    cmd = select(ninja_build_command()),
    toolchains = [
        "@rules_python//python:current_py_toolchain",
        "@bazel_tools//tools/cpp:current_cc_toolchain",
    ],
    tools = [
        "@bazel_tools//tools/cpp:current_cc_toolchain",
        "@rules_python//python:current_py_toolchain",
    ],
)

genrule(
    name = "build",
    srcs = glob(["**"]) + ["crypto_marker"],
    outs = [
        "crypto/libcrypto.a",
        "ssl/libssl.a",
    ],
    cmd = select(boringssl_fips_build_command(
        SUPPORTED_ARCHES,
        STDLIBS,
    )),
    exec_properties = select({
        "@envoy//bazel:engflow_rbe_x86_64": {
            "Pool": "linux_x64_large",
        },
        "@envoy//bazel:engflow_rbe_aarch64": {
            "Pool": "linux_arm64_small",
        },
        "//conditions:default": {},
    }),
    toolchains = ["@bazel_tools//tools/cpp:current_cc_toolchain"],
    tools = [
        ":ninja_bin",
        "@bazel_tools//tools/cpp:current_cc_toolchain",
        "@envoy//bazel/external:boringssl_fips.genrule_cmd",
    ] + select({
        "@platforms//cpu:x86_64": [
            "@fips_cmake_linux_x86_64//:all",
            "@fips_cmake_linux_x86_64//:bin/cmake",
            "@fips_go_linux_amd64//:all",
            "@fips_go_linux_amd64//:bin/go",
        ],
        "@platforms//cpu:aarch64": [
            "@fips_cmake_linux_aarch64//:all",
            "@fips_cmake_linux_aarch64//:bin/cmake",
            "@fips_go_linux_arm64//:all",
            "@fips_go_linux_arm64//:bin/go",
        ],
        "//conditions:default": [],
    }),
)
