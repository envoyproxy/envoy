load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

filegroup(
    name = "configs",
    srcs = glob(
        [
            "**/*.yaml",
        ],
        exclude = [
            "cache/responses.yaml",
            "dynamic-config-fs/**/*",
            "jaeger-native-tracing/*",
            "**/*docker-compose*.yaml",
            # Contrib extensions tested over in contrib.
            "mysql/*",
            "postgres/*",
        ],
    ),
)

genrule(
    name = "contrib_configs",
    srcs = glob(
        [
            "mysql/*.yaml",
            "postgres/*.yaml",
        ],
        exclude = [
            "**/*docker-compose*.yaml",
        ],
    ),
    outs = ["example_configs.tar"],
    cmd = (
        "$(location //configs:configgen.sh) NO_CONFIGGEN $(@D) $(SRCS)"
    ),
    tools = [
        "//configs:configgen.sh",
    ],
)

filegroup(
    name = "certs",
    srcs = glob(["_extra_certs/*.pem"]),
)

filegroup(
    name = "lua",
    srcs = glob(["**/*.lua"]),
)

filegroup(
    name = "files",
    srcs = glob(["**/*"]) + [
        "//examples/wasm-cc:files",
    ],
)
