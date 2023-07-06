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
            "cache/ci-responses.yaml",
            "cache/responses.yaml",
            "dynamic-config-fs/**/*",
            "jaeger-native-tracing/*",
            "opentelemetry/otel-collector-config.yaml",
            "**/*docker-compose*.yaml",
            # Contrib extensions tested over in contrib.
            "golang-http/*.yaml",
            "golang-network/*.yaml",
            "mysql/*",
            "postgres/*",
            "kafka/*.yaml",
        ],
    ),
)

filegroup(
    name = "contrib_configs",
    srcs = glob(
        [
            "golang-http/*.yaml",
            "golang-network/*.yaml",
            "mysql/*.yaml",
            "postgres/*.yaml",
            "kafka/*.yaml",
        ],
        exclude = [
            "**/*docker-compose*.yaml",
        ],
    ),
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
