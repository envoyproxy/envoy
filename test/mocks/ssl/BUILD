package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_mock")

envoy_cc_mock(
    name = "ssl_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//include/envoy/ssl:connection_includes",
        "//include/envoy/ssl:context_config_includes",
        "//include/envoy/ssl:context_includes",
        "//include/envoy/ssl:context_manager_includes",
        "//include/envoy/stats:stats_includes",
    ],
)
