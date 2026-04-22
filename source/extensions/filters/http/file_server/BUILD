load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "absl_status_to_http_status",
    srcs = ["absl_status_to_http_status.cc"],
    hdrs = ["absl_status_to_http_status.h"],
    deps = [
        "//envoy/http:codes_interface",
    ],
)

envoy_cc_library(
    name = "file_server_lib",
    srcs = [
        "file_streamer.cc",
        "filter.cc",
        "filter_config.cc",
    ],
    hdrs = [
        "file_streamer.h",
        "filter.h",
        "filter_config.h",
    ],
    deps = [
        ":absl_status_to_http_status",
        "//envoy/buffer:buffer_interface",
        "//envoy/server:instance_interface",
        "//source/common/common:enum_to_int",
        "//source/common/common:radix_tree_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/common/async_files",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/file_server/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":file_server_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/file_server/v3:pkg_cc_proto",
    ],
)
