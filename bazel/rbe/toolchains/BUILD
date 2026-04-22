load("@envoy_repo//:containers.bzl", "image_gcc")

licenses(["notice"])  # Apache 2

platform(
    name = "rbe_linux_gcc_platform",
    exec_properties = {
        "container-image": "docker://%s" % image_gcc(),
        "dockerAddCapabilities": "SYS_PTRACE,NET_RAW,NET_ADMIN",
        "dockerNetwork": "standard",
    },
    parents = ["//bazel/rbe/toolchains/configs/linux/gcc/config:platform"],
)
