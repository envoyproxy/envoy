package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_mock")

envoy_cc_mock(
    name = "network_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//include/envoy/buffer:buffer_includes",
        "//include/envoy/network:connection_includes",
        "//include/envoy/network:drain_decision_includes",
        "//include/envoy/network:filter_includes",
        "//source/common/network:address_lib",
        "//source/common/network:listener_lib",
        "//source/common/network:utility_lib",
        "//test/mocks/event:event_mocks",
    ],
)
