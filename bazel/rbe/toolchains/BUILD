load("@bazel_toolchains//rules/exec_properties:exec_properties.bzl", "create_rbe_exec_properties_dict")

licenses(["notice"])  # Apache 2

platform(
    name = "rbe_linux_x64_clang_platform",
    constraint_values = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
    exec_properties = create_rbe_exec_properties_dict(
        container_image = "docker://gcr.io/envoy-ci/envoy-build:cf4371e4fae99779fd3c2ee74edfdf45b65dd04a@sha256:8a34c868875877a44861df60396b21334c7f4a2a8ebc1cd31fbfe22f23dd0b9e",
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
        container_image = "docker://gcr.io/envoy-ci/envoy-build:cf4371e4fae99779fd3c2ee74edfdf45b65dd04a@sha256:6143c723473780799230775152b956de4d196b5847666a41fe0c9411455385d0",
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
        container_image = "docker://gcr.io/envoy-ci/envoy-build:cf4371e4fae99779fd3c2ee74edfdf45b65dd04a@sha256:6143c723473780799230775152b956de4d196b5847666a41fe0c9411455385d0",
        docker_add_capabilities = "SYS_PTRACE,NET_RAW,NET_ADMIN",
        docker_network = "standard",
    ),
)
