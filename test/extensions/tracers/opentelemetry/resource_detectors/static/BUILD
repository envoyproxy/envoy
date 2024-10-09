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
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.tracers.opentelemetry.resource_detectors.static_config"],
    rbe_pool = "2core",
    deps = [
        "//envoy/registry",
        "//source/extensions/tracers/opentelemetry/resource_detectors/static:config",
        "//source/extensions/tracers/opentelemetry/resource_detectors/static:static_config_resource_detector_lib",
        "//test/mocks/server:tracer_factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/tracers/opentelemetry/resource_detectors/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "static_config_resource_detector_test",
    srcs = ["static_config_resource_detector_test.cc"],
    extension_names = ["envoy.tracers.opentelemetry.resource_detectors.static_config"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/tracers/opentelemetry/resource_detectors/static:static_config_resource_detector_lib",
        "//test/mocks/server:tracer_factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/tracers/opentelemetry/resource_detectors/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "static_config_resource_detector_integration_test",
    srcs = [
        "static_config_resource_detector_integration_test.cc",
    ],
    extension_names = ["envoy.tracers.opentelemetry.resource_detectors.static_config"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/tracers/opentelemetry:config",
        "//source/extensions/tracers/opentelemetry/resource_detectors/static:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)
