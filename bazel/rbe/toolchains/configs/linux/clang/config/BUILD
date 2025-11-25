load("@envoy_repo//:containers.bzl", "image_worker")

licenses(["notice"])  # Apache 2

package(default_visibility = ["//visibility:public"])

CACHE_SILO_KEY = "llvm-18"

toolchain(
    name = "cc-toolchain",
    exec_compatible_with = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
        "@bazel_tools//tools/cpp:clang",
    ],
    target_compatible_with = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
    toolchain = "//bazel/rbe/toolchains/configs/linux/clang/cc:cc-compiler-k8",
    toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
)

platform(
    name = "platform",
    constraint_values = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
        "@bazel_tools//tools/cpp:clang",
    ],
    exec_properties = {
        "cache-silo-key": CACHE_SILO_KEY,
        "container-image": "docker://%s" % image_worker(),
        "OSFamily": "Linux",
    },
    parents = ["@local_config_platform//:host"],
)

toolchain(
    name = "cc-toolchain-arm64",
    exec_compatible_with = [
        "@platforms//os:linux",
        "@platforms//cpu:aarch64",
        "@bazel_tools//tools/cpp:clang",
    ],
    target_compatible_with = [
        "@platforms//os:linux",
        "@platforms//cpu:aarch64",
    ],
    toolchain = "//bazel/rbe/toolchains/configs/linux/clang/cc:cc-compiler-aarch64",
    toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
)

platform(
    name = "platform-arm64",
    constraint_values = [
        "@platforms//os:linux",
        "@platforms//cpu:aarch64",
        "@bazel_tools//tools/cpp:clang",
    ],
    exec_properties = {
        "container-image": "docker://%s" % image_worker(),
        "OSFamily": "Linux",
        "Pool": "arm",
    },
    parents = ["@local_config_platform//:host"],
)
