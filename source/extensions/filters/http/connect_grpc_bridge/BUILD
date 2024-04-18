load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# L7 HTTP filter that translates Connect RPC to gRPC.

envoy_extension_package()

envoy_cc_library(
    name = "end_stream_response_lib",
    srcs = ["end_stream_response.cc"],
    hdrs = ["end_stream_response.h"],
    deps = [
        "//source/common/json:json_sanitizer_lib",
        "//source/common/runtime:runtime_lib",
    ],
)

envoy_cc_extension(
    name = "filter_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        ":end_stream_response_lib",
        "//source/common/common:assert_lib",
        "//source/common/grpc:codec_lib",
        "//source/common/runtime:runtime_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":filter_lib",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/common/runtime:runtime_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/connect_grpc_bridge/v3:pkg_cc_proto",
    ],
)
