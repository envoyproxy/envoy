load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = [
        "config.cc",
    ],
    hdrs = [
        "config.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/http:filter_interface",
        "//envoy/stream_info:bool_accessor_interface",
        "//envoy/stream_info:filter_state_interface",
        "//source/common/http:codec_client_lib",
        "//source/common/stream_info:bool_accessor_lib",
        "//source/common/tcp_proxy:upstream_lib",
        "@envoy_api//envoy/extensions/upstreams/tcp/generic/v3:pkg_cc_proto",
    ],
)
