licenses(["notice"])  # Apache 2

## WIP: Simple in-memory cache storage plugin. Not ready for deployment.

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_package",
    "envoy_proto_library",
)

envoy_package()

envoy_cc_extension(
    name = "simple_http_cache_lib",
    srcs = ["simple_http_cache.cc"],
    hdrs = ["simple_http_cache.h"],
    security_posture = "robust_to_untrusted_downstream_and_upstream",
    status = "wip",
    deps = [
        ":config_cc_proto",
        "//include/envoy/registry",
        "//include/envoy/runtime:runtime_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:macros",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/protobuf",
        "//source/extensions/filters/http/cache:http_cache_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_proto_library(
    name = "config",
    srcs = ["config.proto"],
)
