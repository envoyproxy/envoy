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
    name = "baggage_propagator_test",
    srcs = ["baggage_propagator_test.cc"],
    extension_names = ["envoy.tracers.opentelemetry"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/tracers/opentelemetry/propagators:propagator_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "w3c_trace_context_propagator_test",
    srcs = ["w3c_trace_context_propagator_test.cc"],
    extension_names = ["envoy.tracers.opentelemetry"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/tracers/opentelemetry/propagators:propagator_lib",
        "//test/test_common:utility_lib",
    ],
)
