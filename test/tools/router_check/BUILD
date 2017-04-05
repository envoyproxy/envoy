package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_binary", "envoy_cc_test_library")

envoy_cc_binary(
    name = "router_check_tool",
    testonly = 1,
    deps = ["router_check_main_lib"],
)

envoy_cc_test_library(
    name = "router_check_main_lib",
    srcs = ["router_check.cc"],
    deps = [
        "//test/tools/router_check/source:router_tool_lib",
    ],
)
