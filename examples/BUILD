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
            "**/*.lua",
        ],
        exclude = [
            "cache/responses.yaml",
            "jaeger-native-tracing/*",
            "**/*docker-compose*.yaml",
        ],
    ),
)
