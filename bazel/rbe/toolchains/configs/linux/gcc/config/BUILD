load("@envoy_repo//:containers.bzl", "image_gcc")

licenses(["notice"])  # Apache 2

package(default_visibility = ["//visibility:public"])

toolchain(
    name = "cc-toolchain",
    exec_compatible_with = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
        "@bazel_tools//tools/cpp:gcc",
    ],
    target_compatible_with = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
    toolchain = "//bazel/rbe/toolchains/configs/linux/gcc/cc:cc-compiler-k8",
    toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
)

platform(
    name = "platform",
    constraint_values = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
        "@bazel_tools//tools/cpp:gcc",
    ],
    exec_properties = {
        "container-image": "docker://%s" % image_gcc(),
        "OSFamily": "Linux",
    },
    parents = ["@local_config_platform//:host"],
)
