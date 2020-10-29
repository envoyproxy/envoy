licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_binary",
    "envoy_cc_test_library",
    "envoy_package",
)

envoy_package()

envoy_cc_test_library(
    name = "config_load_check_lib",
    srcs = ["config_load_check.cc"],
    deps = [
        "//source/common/config:protobuf_link_hacks",
        "//test/config_test:config_test_lib",
    ],
)

envoy_cc_test_binary(
    name = "config_load_check_tool",
    deps = [
        ":config_load_check_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:thread_lib",
        "//source/common/config:protobuf_link_hacks",
        "//source/common/event:libevent_lib",
    ],
)
