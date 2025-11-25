load("@bazel_toolchains//rules/exec_properties:exec_properties.bzl", "create_rbe_exec_properties_dict")
load("@envoy_repo//:containers.bzl", "image_worker")

licenses(["notice"])  # Apache 2

package(default_visibility = ["//visibility:public"])

platform(
    name = "macos",
    exec_properties = {
        "Pool": "macos15",
    },
    parents = ["//bazel/platforms:macos_arm64"],
)

platform(
    name = "linux_x64",
    exec_properties = create_rbe_exec_properties_dict(
        container_image = "docker://%s" % image_worker(),
        docker_add_capabilities = "SYS_PTRACE,NET_RAW,NET_ADMIN",
        docker_network = "standard",
        docker_privileged = True,
    ),
    parents = ["//bazel/rbe/toolchains/configs/linux/clang/config:platform"],
)

platform(
    name = "linux_arm64",
    exec_properties = create_rbe_exec_properties_dict(
        container_image = "docker://%s" % image_worker(),
        docker_add_capabilities = "SYS_PTRACE,NET_RAW,NET_ADMIN",
        docker_network = "standard",
        docker_privileged = True,
    ) | {
        "Pool": "arm",
    },
    parents = ["//bazel/rbe/toolchains/configs/linux/clang/config:platform-arm64"],
)
