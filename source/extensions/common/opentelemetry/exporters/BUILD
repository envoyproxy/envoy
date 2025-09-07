load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "exporters_lib",
    deps = [
        "//source/extensions/common/opentelemetry/exporters/otlp:trace_exporter_lib",
    ],
)
