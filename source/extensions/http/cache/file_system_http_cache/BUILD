load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
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
    srcs = glob(["*.cc"]),
    hdrs = glob(["*.h"]),
    deps = [
        ":cache_file_header_proto_cc_proto",
        "//envoy/common:time_interface",
        "//envoy/http:header_map_interface",
        "//envoy/registry",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:macros",
        "//source/common/common:safe_memcpy_lib",
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
