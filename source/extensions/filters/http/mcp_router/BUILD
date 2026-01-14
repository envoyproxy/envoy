load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "session_codec_lib",
    srcs = ["session_codec.cc"],
    hdrs = ["session_codec.h"],
    deps = [
        "//source/common/common:base64_lib",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

envoy_cc_library(
    name = "filter_config_lib",
    srcs = ["filter_config.cc"],
    hdrs = ["filter_config.h"],
    deps = [
        "//envoy/server:filter_config_interface",
        "@com_google_absl//absl/types:variant",
        "@envoy_api//envoy/extensions/filters/http/mcp_router/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "mcp_router_lib",
    srcs = ["mcp_router.cc"],
    hdrs = ["mcp_router.h"],
    deps = [
        ":filter_config_lib",
        ":session_codec_lib",
        "//envoy/http:async_client_interface",
        "//envoy/http:filter_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:fmt_lib",
        "//source/common/common:logger_lib",
        "//source/common/config:metadata_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:muxdemux_lib",
        "//source/common/http:utility_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/json:json_streamer_lib",
        "//source/common/protobuf:utility_lib",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":mcp_router_lib",
        "//envoy/registry",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/mcp_router/v3:pkg_cc_proto",
    ],
)
