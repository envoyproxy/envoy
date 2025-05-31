load("@bazel_toolchains//rules/exec_properties:exec_properties.bzl", "create_rbe_exec_properties_dict")

licenses(["notice"])  # Apache 2

platform(
    name = "rbe_linux_x64_platform",
    constraint_values = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
    exec_properties = create_rbe_exec_properties_dict(
        docker_add_capabilities = "SYS_PTRACE,NET_RAW,NET_ADMIN",
        docker_network = "standard",
        docker_privileged = True,
        container_image = "docker://gcr.io/envoy-ci/envoy-build@sha256:a41f69c56ba636856576949a22cc2ec450c48a7a37487554d8a31827e0d54c03",
    ),
)

platform(
    name = "rbe_linux_arm64_platform",
    constraint_values = [
        "@platforms//os:linux",
        "@platforms//cpu:aarch64",
    ],
    exec_properties = create_rbe_exec_properties_dict(
        docker_add_capabilities = "SYS_PTRACE,NET_RAW,NET_ADMIN",
        docker_network = "standard",
        docker_privileged = True,
        container_image = "docker://gcr.io/envoy-ci/envoy-build@sha256:a41f69c56ba636856576949a22cc2ec450c48a7a37487554d8a31827e0d54c03",
    ),
)

platform(
    name = "rbe_linux_gcc_platform",
    exec_properties = create_rbe_exec_properties_dict(
        docker_add_capabilities = "SYS_PTRACE,NET_RAW,NET_ADMIN",
        docker_network = "standard",
        container_image = "docker://gcr.io/envoy-ci/envoy-build@sha256:a41f69c56ba636856576949a22cc2ec450c48a7a37487554d8a31827e0d54c03",
    ),
)
