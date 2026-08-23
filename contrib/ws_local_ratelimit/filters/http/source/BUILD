load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

# WebSocket local rate limit L7 HTTP filter

envoy_cc_library(
    name = "ws_local_ratelimit_filter_lib",
    srcs = ["ws_local_ratelimit_filter.cc"],
    hdrs = ["ws_local_ratelimit_filter.h"],
    deps = [
        "//envoy/http:filter_interface",
        "//envoy/stream_info:filter_state_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:macros",
        "//source/common/http:utility_lib",
        "//source/common/websocket:codec_lib",
        "//source/extensions/filters/common/local_ratelimit:local_ratelimit_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/ws_local_ratelimit/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":ws_local_ratelimit_filter_lib",
        "//envoy/registry",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/ws_local_ratelimit/v3alpha:pkg_cc_proto",
    ],
)
