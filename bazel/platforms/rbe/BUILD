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
    exec_properties = {
        "container-image": "docker://%s" % image_worker(),
        "dockerAddCapabilities": "SYS_PTRACE,NET_RAW,NET_ADMIN",
        "dockerNetwork": "standard",
        "dockerPrivileged": "True",
    },
    parents = ["//bazel/platforms:linux_x64"],
)

platform(
    name = "linux_arm64",
    exec_properties = {
        "container-image": "docker://%s" % image_worker(),
        "dockerAddCapabilities": "SYS_PTRACE,NET_RAW,NET_ADMIN",
        "dockerNetwork": "standard",
        "dockerPrivileged": "True",
        "Pool": "arm",
    },
    parents = ["//bazel/platforms:linux_arm64"],
)
