package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_library")

envoy_cc_library(
    name = "access_log_formatter_lib",
    srcs = ["access_log_formatter.cc"],
    hdrs = ["access_log_formatter.h"],
    deps = [
        "//include/envoy/http:access_log_includes",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "access_log_lib",
    srcs = ["access_log_impl.cc"],
    hdrs = ["access_log_impl.h"],
    deps = [
        "//include/envoy/access_log:access_log_includes",
        "//include/envoy/filesystem:filesystem_includes",
        "//include/envoy/http:access_log_includes",
        "//include/envoy/http:header_map_includes",
        "//include/envoy/runtime:runtime_includes",
        "//include/envoy/upstream:upstream_includes",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/runtime:uuid_util_lib",
        "//source/common/tracing:http_tracer_lib",
    ],
)

envoy_cc_library(
    name = "request_info_lib",
    hdrs = ["request_info_impl.h"],
    deps = ["//include/envoy/http:access_log_includes"],
)
