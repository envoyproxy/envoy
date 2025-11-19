load("@envoy_repo//:containers.bzl", "image_mobile")

licenses(["notice"])  # Apache 2

package(default_visibility = ["//visibility:public"])

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
    toolchain = "//third_party/rbe_configs/cc:cc-compiler-k8",
    toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
)

platform(
    name = "platform",
    parents = ["@local_config_platform//:host"],
    constraint_values = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
        "@bazel_tools//tools/cpp:clang",
    ],
    exec_properties = {
        "container-image": "docker://%s" % image_mobile(),
        "OSFamily": "Linux",
        "Pool": "linux",
    },
)

platform(
    name = "platform-asan",
    parents = ["@local_config_platform//:host"],
    constraint_values = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
        "@bazel_tools//tools/cpp:clang",
    ],
    exec_properties = {
        "container-image": "docker://%s" % image_mobile(),
        "OSFamily": "Linux",
        "Pool": "linux",
        # Necessary to workaround https://github.com/google/sanitizers/issues/916, otherwise, dangling threads in the
        # docker container fail tests on teardown (example: https://github.com/envoyproxy/envoy-mobile/runs/3443649963)
        "dockerAddCapabilities": "SYS_PTRACE",
    },
)
