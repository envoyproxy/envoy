load("@bazel_toolchains//rules/exec_properties:exec_properties.bzl", "create_rbe_exec_properties_dict")

licenses(["notice"])  # Apache 2

platform(
    name = "rbe_linux_x64_platform",
    constraint_values = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
    exec_properties = create_rbe_exec_properties_dict(
        container_image = "docker://gcr.io/envoy-ci/envoy-build@sha256:92a34e0d3e878d596c8330722d8195bd61d905b0f5ba502363fe3845f1c6d479",
        docker_add_capabilities = "SYS_PTRACE,NET_RAW,NET_ADMIN",
        docker_network = "standard",
        docker_privileged = True,
    ),
)

platform(
    name = "rbe_linux_arm64_platform",
    constraint_values = [
        "@platforms//os:linux",
        "@platforms//cpu:aarch64",
    ],
    exec_properties = create_rbe_exec_properties_dict(
        container_image = "docker://gcr.io/envoy-ci/envoy-build@sha256:92a34e0d3e878d596c8330722d8195bd61d905b0f5ba502363fe3845f1c6d479",
        docker_add_capabilities = "SYS_PTRACE,NET_RAW,NET_ADMIN",
        docker_network = "standard",
        docker_privileged = True,
    ) | {
        "Pool": "arm",
    },
)

platform(
    name = "rbe_linux_gcc_platform",
    exec_properties = create_rbe_exec_properties_dict(
        container_image = "docker://gcr.io/envoy-ci/envoy-build@sha256:92a34e0d3e878d596c8330722d8195bd61d905b0f5ba502363fe3845f1c6d479",
        docker_add_capabilities = "SYS_PTRACE,NET_RAW,NET_ADMIN",
        docker_network = "standard",
    ),
)
