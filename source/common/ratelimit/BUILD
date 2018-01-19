licenses(["notice"])  # Apache 2

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
    external_deps = ["envoy_bootstrap"],
    deps = [
        ":ratelimit_proto",
        "//include/envoy/grpc:async_client_interface",
        "//include/envoy/grpc:async_client_manager_interface",
        "//include/envoy/ratelimit:ratelimit_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/http:headers_lib",
        "//source/common/tracing:http_tracer_lib",
    ],
)

envoy_proto_library(
    name = "ratelimit_proto",
    srcs = ["ratelimit.proto"],
)
