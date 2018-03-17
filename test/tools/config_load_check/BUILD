licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
    "envoy_cc_test_library",
    "envoy_package",
)

envoy_package()

envoy_cc_test_library(
    name = "config_load_check_lib",
    srcs = ["config_load_check.cc"],
    deps = ["//test/config_test:config_test_lib"],
)

envoy_cc_binary(
    name = "config_load_check_tool",
    testonly = 1,
    deps = [
        ":config_load_check_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:thread_lib",
        "//source/common/event:libevent_lib",
    ],
)
