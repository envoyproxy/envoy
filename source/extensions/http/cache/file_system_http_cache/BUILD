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
    name = "cache_file_header_proto",
    srcs = ["cache_file_header.proto"],
    deps = ["//source/extensions/filters/http/cache:key"],
)

envoy_cc_extension(
    name = "config",
    srcs = [
        "cache_eviction_thread.cc",
        "config.cc",
        "file_system_http_cache.cc",
        "insert_context.cc",
        "lookup_context.cc",
        "stats.cc",
    ],
    hdrs = [
        "cache_eviction_thread.h",
        "file_system_http_cache.h",
        "insert_context.h",
        "lookup_context.h",
        "stats.h",
    ],
    deps = [
        ":cache_file_fixed_block",
        ":cache_file_header_proto_cc_proto",
        ":cache_file_header_proto_util",
        "//envoy/common:time_interface",
        "//envoy/http:header_map_interface",
        "//envoy/registry",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:macros",
        "//source/common/common:safe_memcpy_lib",
        "//source/common/filesystem:directory_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/protobuf",
        "//source/extensions/common/async_files",
        "//source/extensions/filters/http/cache:http_cache_lib",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:optional",
        "@envoy_api//envoy/extensions/http/cache/file_system_http_cache/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "cache_file_header_proto_util",
    srcs = ["cache_file_header_proto_util.cc"],
    hdrs = ["cache_file_header_proto_util.h"],
    deps = [
        ":cache_file_header_proto_cc_proto",
        "//envoy/http:header_map_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:macros",
        "//source/extensions/filters/http/cache:http_cache_lib",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/strings",
    ],
)

envoy_cc_library(
    name = "cache_file_fixed_block",
    srcs = ["cache_file_fixed_block.cc"],
    hdrs = ["cache_file_fixed_block.h"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "@com_google_absl//absl/strings",
    ],
)
