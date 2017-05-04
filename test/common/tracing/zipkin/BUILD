licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "zipkin_test",
    srcs = [
        "span_buffer_test.cc",
        "span_context_test.cc",
        "tracer_test.cc",
        "util_test.cc",
        "zipkin_core_types_test.cc",
    ],
    deps = [
        "//include/envoy/common:optional",
        "//include/envoy/common:time_interface",
        "//include/envoy/runtime:runtime_interface",
        "//source/common/common:hex_lib",
        "//source/common/common:utility_lib",
        "//source/common/network:address_lib",
        "//source/common/tracing/zipkin:zipkin_lib",
        "//test/mocks:common_lib",
        "//test/mocks/runtime:runtime_mocks",
    ],
)
