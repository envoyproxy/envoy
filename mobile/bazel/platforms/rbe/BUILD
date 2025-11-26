load("@bazel_toolchains//rules/exec_properties:exec_properties.bzl", "create_rbe_exec_properties_dict")
load("@envoy_repo//:containers.bzl", "image_mobile")

licenses(["notice"])  # Apache 2

package(default_visibility = ["//visibility:public"])

platform(
    name = "linux_x64",
    exec_properties = create_rbe_exec_properties_dict(
        container_image = "docker://%s" % image_mobile(),
    ) | {
        "Pool": "linux",
    },
    parents = ["@envoy//bazel/platforms:linux_x64"],
)

platform(
    name = "linux_arm64",
    exec_properties = create_rbe_exec_properties_dict(
        container_image = "docker://%s" % image_mobile(),
    ) | {
        "Pool": "linux",
    },
    parents = ["@envoy//bazel/platforms:linux_arm64"],
)
