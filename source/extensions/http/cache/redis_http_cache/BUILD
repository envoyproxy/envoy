load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_proto_library(
    name = "cache_header_proto",
    srcs = ["cache_header.proto"],
    deps = ["//source/extensions/filters/http/cache:key"],
)

envoy_cc_extension(
    name = "config",
    srcs = [
        "config.cc",
        "redis_http_cache.cc",
        "cache_header_proto_util.cc",
        "redis_http_cache_lookup.cc",
        "redis_http_cache_insert.cc",
        "redis_http_cache_client.cc",
    ],
    hdrs = [
        "redis_http_cache.h",
        "redis_http_cache_lookup.h",
        "redis_http_cache_insert.h",
        "redis_http_cache_client.h",
        "cache_header_proto_util.h",
    ],
    deps = [
        ":cache_header_proto_cc_proto",
        "//source/extensions/filters/http/cache:http_cache_lib",
        "//source/extensions/common/redis:async_redis_client_lib",
        "@envoy_api//envoy/extensions/http/cache/redis_http_cache/v3:pkg_cc_proto",
    ],
)


