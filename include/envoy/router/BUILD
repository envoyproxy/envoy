package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_library")

envoy_cc_library(
    name = "rds_includes",
    hdrs = ["rds.h"],
    deps = [":router_includes"],
)

envoy_cc_library(
    name = "router_includes",
    hdrs = ["router.h"],
    deps = [
        "//include/envoy/common:optional_includes",
        "//include/envoy/http:codec_includes",
        "//include/envoy/http:header_map_includes",
        "//include/envoy/upstream:resource_manager_includes",
    ],
)

envoy_cc_library(
    name = "router_ratelimit_includes",
    hdrs = ["router_ratelimit.h"],
    deps = [
        "//include/envoy/http:filter_includes",
        "//include/envoy/http:header_map_includes",
        "//include/envoy/ratelimit:ratelimit_includes",
    ],
)

envoy_cc_library(
    name = "shadow_writer_includes",
    hdrs = ["shadow_writer.h"],
    deps = ["//include/envoy/http:message_includes"],
)
