load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "per_route_resource_detector_integration_test",
    srcs = [
        "per_route_resource_detector_integration_test.cc",
    ],
    extension_names = ["envoy.tracers.opentelemetry.resource_detectors.per_route"],
    deps = [
        "//source/exe:main_common_lib",
        "//source/extensions/tracers/opentelemetry:config",
        "//source/extensions/tracers/opentelemetry/resource_detectors/per_route:config",
        "//source/extensions/tracers/opentelemetry/resource_detectors/per_route:resource_typed_metadata",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "//third_party/opentelemetry/proto/collector/trace/v1:trace_service_cc_proto",
    ],
)
