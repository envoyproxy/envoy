load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "resource_provider_test",
    srcs = ["resource_provider_test.cc"],
    deps = [
        "//envoy/registry",
        "//source/extensions/tracers/opentelemetry/resource_detectors:resource_detector_lib",
        "//test/mocks/server:tracer_factory_context_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)
