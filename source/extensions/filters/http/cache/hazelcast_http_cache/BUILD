licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_package",
    "envoy_proto_library",
)

envoy_package()

envoy_cc_extension(
    name = "hazelcast_http_cache_lib",
    srcs = [
        "hazelcast_cache_entry.cc",
        "hazelcast_context.cc",
        "hazelcast_http_cache.cc",
        "hazelcast_storage_accessor.cc",
    ],
    hdrs = [
        "hazelcast_cache_entry.h",
        "hazelcast_context.h",
        "hazelcast_http_cache.h",
        "hazelcast_storage_accessor.h",
        "util.h",
    ],
    external_deps = ["hazelcast_cpp_client"],
    security_posture = "robust_to_untrusted_downstream_and_upstream",
    status = "wip",
    deps = [
        ":config_cc_proto",
        "//include/envoy/registry",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:logger_lib",
        "//source/common/http:header_map_lib",
        "//source/common/runtime:runtime_lib",
        "//source/extensions/filters/http/cache:http_cache_lib",
    ],
)

envoy_proto_library(
    name = "config",
    srcs = ["config.proto"],
    deps = ["@envoy_api//envoy/config/core/v3:pkg"],
)
