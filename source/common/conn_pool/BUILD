load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "pending_stream_lib",
    hdrs = ["pending_stream.h"],
    deps = [
        "//envoy/common:conn_pool_interface",
        "//source/common/common:linked_object",
    ],
)

envoy_cc_library(
    name = "conn_pool_base_lib",
    srcs = [
        "conn_pool_base.cc",
        "pending_stream.cc",
    ],
    hdrs = ["conn_pool_base.h"],
    deps = [
        ":pending_stream_lib",
        "//envoy/stats:timespan_interface",
        "//source/common/common:debug_recursion_checker_lib",
        "//source/common/common:linked_object",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:message_validator_lib",
        "//source/common/queue_policy:queue_policy_base_lib",
        "//source/common/stats:timespan_lib",
        "//source/common/upstream:upstream_lib",
    ],
)
