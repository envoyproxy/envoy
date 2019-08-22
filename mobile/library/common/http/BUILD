licenses(["notice"])  # Apache 2

load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_package")

envoy_package()

envoy_cc_library(
    name = "dispatcher_lib",
    srcs = ["dispatcher.cc"],
    hdrs = ["dispatcher.h"],
    repository = "@envoy",
    deps = [
        "//library/common/buffer:utility_lib",
        "//library/common/http:header_utility_lib",
        "//library/common/types:c_types_lib",
        "@envoy//include/envoy/buffer:buffer_interface",
        "@envoy//include/envoy/event:dispatcher_interface",
        "@envoy//include/envoy/http:async_client_interface",
        "@envoy//include/envoy/http:header_map_interface",
        "@envoy//include/envoy/upstream:cluster_manager_interface",
        "@envoy//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "header_utility_lib",
    srcs = ["header_utility.cc"],
    hdrs = ["header_utility.h"],
    repository = "@envoy",
    deps = [
        "//library/common/types:c_types_lib",
        "@envoy//include/envoy/buffer:buffer_interface",
        "@envoy//include/envoy/http:header_map_interface",
        "@envoy//source/common/http:header_map_lib",
    ],
)
