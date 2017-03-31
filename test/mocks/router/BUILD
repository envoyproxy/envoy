package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_mock")

envoy_cc_mock(
    name = "router_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//include/envoy/router:router_includes",
        "//include/envoy/router:router_ratelimit_includes",
        "//include/envoy/router:shadow_writer_includes",
        "//test/mocks:common_lib",
    ],
)
