load("@envoy_repo//:containers.bzl", "image_worker")

licenses(["notice"])  # Apache 2

platform(
    name = "rbe_linux_gcc_platform",
    constraint_values = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
    exec_properties = {
        "container-image": "docker://%s" % image_worker(),
        "dockerAddCapabilities": "SYS_PTRACE,NET_RAW,NET_ADMIN",
        "dockerNetwork": "standard",
    },
    parents = ["@platforms//host"],
)
