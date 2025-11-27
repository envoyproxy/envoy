load("@bazel_toolchains//rules/exec_properties:exec_properties.bzl", "create_rbe_exec_properties_dict")
load("@envoy_repo//:containers.bzl", "image_gcc")

licenses(["notice"])  # Apache 2

platform(
    name = "rbe_linux_gcc_platform",
    exec_properties = create_rbe_exec_properties_dict(
        container_image = "docker://%s" % image_gcc(),
        docker_add_capabilities = "SYS_PTRACE,NET_RAW,NET_ADMIN",
        docker_network = "standard",
    ),
    parents = ["//bazel/rbe/toolchains/configs/linux/gcc/config:platform"],
)
