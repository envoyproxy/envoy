load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "trace_context_test",
    srcs = ["trace_context_test.cc"],
    deps = [
        "//source/extensions/propagators/w3c:trace_context_lib",
        "@com_google_googletest//:gtest_main",
    ],
)

envoy_cc_test(
    name = "propagator_test",
    srcs = ["propagator_test.cc"],
    deps = [
        "//source/common/http:header_map_lib",
        "//source/common/tracing:trace_context_lib",
        "//source/extensions/propagators/w3c:propagator_lib",
        "@com_google_googletest//:gtest_main",
    ],
)
