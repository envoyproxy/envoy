package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_library")

envoy_cc_library(
    name = "buffer_filter_lib",
    srcs = ["buffer_filter.cc"],
    hdrs = ["buffer_filter.h"],
    deps = [
        "//include/envoy/event:dispatcher_includes",
        "//include/envoy/event:timer_includes",
        "//include/envoy/http:codes_includes",
        "//include/envoy/http:filter_includes",
        "//include/envoy/stats:stats_includes",
        "//include/envoy/stats:stats_macros",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:enum_to_int",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
    ],
)

envoy_cc_library(
    name = "fault_filter_lib",
    srcs = ["fault_filter.cc"],
    hdrs = ["fault_filter.h"],
    deps = [
        "//include/envoy/event:timer_includes",
        "//include/envoy/http:codes_includes",
        "//include/envoy/http:filter_includes",
        "//include/envoy/http:header_map_includes",
        "//include/envoy/runtime:runtime_includes",
        "//include/envoy/stats:stats_includes",
        "//include/envoy/stats:stats_macros",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/json:config_schemas_lib",
        "//source/common/router:config_lib",
    ],
)

envoy_cc_library(
    name = "ratelimit_lib",
    srcs = ["ratelimit.cc"],
    deps = [
        ":ratelimit_lib_includes",
        "//include/envoy/http:codes_includes",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:enum_to_int",
        "//source/common/http:codes_lib",
        "//source/common/router:config_lib",
    ],
)

envoy_cc_library(
    name = "ratelimit_lib_includes",
    hdrs = ["ratelimit.h"],
    deps = [
        "//include/envoy/http:filter_includes",
        "//include/envoy/local_info:local_info_includes",
        "//include/envoy/ratelimit:ratelimit_includes",
        "//include/envoy/runtime:runtime_includes",
        "//include/envoy/upstream:cluster_manager_includes",
        "//source/common/common:assert_lib",
        "//source/common/http:header_map_lib",
        "//source/common/json:config_schemas_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/json:json_validator_lib",
    ],
)
