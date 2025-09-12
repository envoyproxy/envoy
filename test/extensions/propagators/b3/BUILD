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
    name = "propagator_test",
    srcs = ["propagator_test.cc"],
    extension_names = ["envoy.tracers.opentelemetry"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/propagators/b3:b3_propagator_lib",
        "//test/test_common:utility_lib",
    ],
)
