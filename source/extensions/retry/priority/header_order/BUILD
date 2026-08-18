load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "header_order_lib",
    srcs = ["header_order.cc"],
    hdrs = ["header_order.h"],
    deps = [
        "//envoy/stream_info:stream_info_interface",
        "//envoy/upstream:retry_interface",
        "//source/common/config:metadata_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":header_order_lib",
        "//envoy/registry",
        "//envoy/upstream:retry_interface",
        "//source/common/protobuf",
        "@envoy_api//envoy/extensions/retry/priority/header_order/v3:pkg_cc_proto",
    ],
)
