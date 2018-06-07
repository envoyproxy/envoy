licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "buffer_helper_lib",
    srcs = ["buffer_helper.cc"],
    hdrs = ["buffer_helper.h"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:byte_order_lib",
    ],
)

envoy_cc_library(
    name = "protocol_lib",
    srcs = [
        "binary_protocol.cc",
        "compact_protocol.cc",
        "protocol.cc",
    ],
    hdrs = [
        "binary_protocol.h",
        "compact_protocol.h",
        "protocol.h",
    ],
    external_deps = ["abseil_optional"],
    deps = [
        ":buffer_helper_lib",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "transport_lib",
    srcs = ["transport.cc"],
    hdrs = ["transport.h"],
    deps = [
        ":buffer_helper_lib",
        ":protocol_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
        "//source/common/singleton:const_singleton",
    ],
)
