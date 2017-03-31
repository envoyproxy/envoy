package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_proto_library")

envoy_cc_library(
    name = "ratelimit_lib",
    srcs = ["ratelimit_impl.cc"],
    hdrs = ["ratelimit_impl.h"],
    deps = [
        ":ratelimit_proto",
        "//include/envoy/grpc:rpc_channel_includes",
        "//include/envoy/ratelimit:ratelimit_includes",
        "//include/envoy/tracing:context_includes",
        "//include/envoy/upstream:cluster_manager_includes",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/grpc:rpc_channel_lib",
        "//source/common/http:headers_lib",
        "//source/common/json:json_loader_lib",
    ],
)

envoy_proto_library(
    name = "ratelimit_proto",
    srcs = ["ratelimit.proto"],
)
