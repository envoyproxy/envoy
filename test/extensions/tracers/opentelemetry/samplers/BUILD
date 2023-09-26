load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "sampler_test",
    srcs = ["sampler_test.cc"],
    deps = [
        "//envoy/registry",
        "//source/extensions/tracers/opentelemetry/samplers:sampler_lib",
        "//test/mocks/server:tracer_factory_context_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)