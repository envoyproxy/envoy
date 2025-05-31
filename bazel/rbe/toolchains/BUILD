load("@bazel_toolchains//rules/exec_properties:exec_properties.bzl", "create_rbe_exec_properties_dict")

licenses(["notice"])  # Apache 2

platform(
    name = "rbe_linux_x64_clang_platform",
    constraint_values = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
    exec_properties = create_rbe_exec_properties_dict(
        container_image = "docker://gcr.io/envoy-ci/envoy-build@sha256:cd3e44f040477c1cc27b0b464614a477b98c8210b6624cfeb926a5d69e4cc6f6",
        docker_add_capabilities = "SYS_PTRACE,NET_RAW,NET_ADMIN",
        docker_network = "standard",
        docker_privileged = True,
    ),
)

platform(
    name = "rbe_linux_arm64_clang_platform",
    constraint_values = [
        "@platforms//os:linux",
        "@platforms//cpu:aarch64",
    ],
    exec_properties = create_rbe_exec_properties_dict(
        container_image = "docker://gcr.io/envoy-ci/envoy-build@sha256:cd3e44f040477c1cc27b0b464614a477b98c8210b6624cfeb926a5d69e4cc6f6",
        docker_add_capabilities = "SYS_PTRACE,NET_RAW,NET_ADMIN",
        docker_network = "standard",
        docker_privileged = True,
    ) | {
        "Pool": "arm",
    },
)

platform(
    name = "rbe_linux_x64_gcc_platform",
    exec_properties = create_rbe_exec_properties_dict(
        container_image = "docker://gcr.io/envoy-ci/envoy-build@sha256:cd3e44f040477c1cc27b0b464614a477b98c8210b6624cfeb926a5d69e4cc6f6",
        docker_add_capabilities = "SYS_PTRACE,NET_RAW,NET_ADMIN",
        docker_network = "standard",
    ),
)
