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
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/tracers/opentelemetry/resource_detectors/v3:pkg_cc_proto",
        "@opentelemetry-cpp//api",
        "@opentelemetry-proto//:trace_proto_cc",
        "@opentelemetry-proto//:trace_service_proto_cc",
    ],
)
