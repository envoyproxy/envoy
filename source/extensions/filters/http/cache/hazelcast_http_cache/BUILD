licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_proto_library",
    "envoy_package",
)

envoy_package()

envoy_cc_extension(
    name = "hazelcast_http_cache_lib",
    srcs = ["hazelcast_http_cache.cc",
            "hazelcast_cache_entry.cc",
            "hazelcast_context.cc"],
    hdrs = ["hazelcast_http_cache.h",
            "hazelcast_cache_entry.h",
            "hazelcast_context.h",
            "config_util.h"],
    status = "wip",
    security_posture = "robust_to_untrusted_downstream_and_upstream",
    external_deps = ["hazelcast_cpp_client"],
    deps = [
        ":config_cc_proto",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:logger_lib",
        "//source/common/http:header_map_lib",
        "//include/envoy/registry",
        "//source/extensions/filters/http/cache:http_cache_lib",
    ],
)

envoy_proto_library(
    name = "config",
    srcs = ["config.proto"],
)
