package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_library")

envoy_cc_library(
    name = "buffer_lib",
    srcs = ["buffer.cc"],
    hdrs = ["buffer.h"],
    deps = [
        "//include/envoy/server:instance_includes",
        "//source/common/http/filter:buffer_filter_lib",
        "//source/common/json:config_schemas_lib",
        "//source/server/config/network:http_connection_manager_lib",
    ],
    alwayslink = 1,
)

envoy_cc_library(
    name = "dynamo_lib",
    srcs = ["dynamo.cc"],
    hdrs = ["dynamo.h"],
    deps = [
        "//source/common/dynamo:dynamo_filter_lib",
        "//source/server/config/network:http_connection_manager_lib",
    ],
    alwayslink = 1,
)

envoy_cc_library(
    name = "fault_lib",
    srcs = ["fault.cc"],
    hdrs = ["fault.h"],
    deps = [
        "//include/envoy/server:instance_includes",
        "//source/common/http/filter:fault_filter_lib",
        "//source/common/json:config_schemas_lib",
        "//source/server/config/network:http_connection_manager_lib",
    ],
    alwayslink = 1,
)

envoy_cc_library(
    name = "grpc_http1_bridge_lib",
    srcs = ["grpc_http1_bridge.cc"],
    hdrs = ["grpc_http1_bridge.h"],
    deps = [
        "//include/envoy/server:instance_includes",
        "//source/common/grpc:http1_bridge_filter_lib",
        "//source/server/config/network:http_connection_manager_lib",
    ],
    alwayslink = 1,
)

envoy_cc_library(
    name = "ratelimit_lib",
    srcs = ["ratelimit.cc"],
    hdrs = ["ratelimit.h"],
    deps = [
        "//include/envoy/server:instance_includes",
        "//source/common/http/filter:ratelimit_lib",
        "//source/common/http/filter:ratelimit_lib_includes",
        "//source/server/config/network:http_connection_manager_lib",
    ],
    alwayslink = 1,
)

envoy_cc_library(
    name = "router_lib",
    srcs = ["router.cc"],
    hdrs = ["router.h"],
    deps = [
        "//include/envoy/server:instance_includes",
        "//source/common/json:config_schemas_lib",
        "//source/common/router:router_lib",
        "//source/common/router:shadow_writer_lib",
        "//source/server/config/network:http_connection_manager_lib",
    ],
    alwayslink = 1,
)
