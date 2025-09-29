load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "opentelemetry_sdk_lib",
    deps = [
        "//source/extensions/common/opentelemetry/sdk/common:sdk_common_lib",
        "//source/extensions/common/opentelemetry/sdk/trace:sdk_trace_lib",
        "//source/extensions/common/opentelemetry/sdk/metrics:sdk_metrics_lib",
        "//source/extensions/common/opentelemetry/sdk/logs:sdk_logs_lib",
        # Additional signal-specific libraries will be added here as they are implemented
        # "//source/extensions/common/opentelemetry/sdk/resource:sdk_resource_lib",
        # "//source/extensions/common/opentelemetry/sdk/configuration:sdk_configuration_lib",
    ],
)
