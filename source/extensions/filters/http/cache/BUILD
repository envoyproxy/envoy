licenses(["notice"])  # Apache 2

## Pluggable HTTP cache filter

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
    "envoy_proto_library",
)

envoy_package()

envoy_proto_library(
    name = "key",
    srcs = ["key.proto"],
)

envoy_cc_library(
    name = "http_cache_lib",
    srcs = ["http_cache.cc"],
    hdrs = ["http_cache.h"],
    deps = [
        ":http_cache_utils_lib",
        ":key_cc_proto",
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/common:time_interface",
        "//include/envoy/config:typed_config_interface",
        "//include/envoy/http:codes_interface",
        "//include/envoy/http:header_map_interface",
        "//source/common/common:assert_lib",
        "//source/common/http:headers_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_library(
    name = "http_cache_utils_lib",
    srcs = ["http_cache_utils.cc"],
    hdrs = ["http_cache_utils.h"],
    deps = [
        "//include/envoy/common:time_interface",
        "//include/envoy/http:header_map_interface",
    ],
)

envoy_cc_extension(
    name = "config",
    security_posture = "robust_to_untrusted_downstream_and_upstream",
    status = "wip",
)
