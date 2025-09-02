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
    name = "composite_propagator_test",
    srcs = ["composite_propagator_test.cc"],
    extension_names = ["envoy.tracers.opentelemetry"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/tracers/opentelemetry/propagators:propagator_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "propagator_factory_test",
    srcs = ["propagator_factory_test.cc"],
    extension_names = ["envoy.tracers.opentelemetry"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/tracers/opentelemetry/propagators:propagator_lib",
        "//test/mocks/api:api_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)
