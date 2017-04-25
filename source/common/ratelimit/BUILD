load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
    "envoy_proto_library",
)

envoy_package()

envoy_cc_library(
    name = "ratelimit_lib",
    srcs = ["ratelimit_impl.cc"],
    hdrs = ["ratelimit_impl.h"],
    deps = [
        ":ratelimit_proto",
        "//include/envoy/grpc:rpc_channel_interface",
        "//include/envoy/ratelimit:ratelimit_interface",
        "//include/envoy/tracing:context_interface",
        "//include/envoy/upstream:cluster_manager_interface",
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
