load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "http_capsule_filter_lib",
    srcs = ["http_capsule.cc"],
    hdrs = ["http_capsule.h"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:hex_lib",
        "//source/extensions/filters/udp/udp_proxy/session_filters:filter_interface",
        "@com_github_google_quiche//:quiche_common_capsule_lib",
        "@com_github_google_quiche//:quiche_common_connect_udp_datagram_payload_lib",
        "@envoy_api//envoy/extensions/filters/udp/udp_proxy/session/http_capsule/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":http_capsule_filter_lib",
        "//source/extensions/filters/udp/udp_proxy/session_filters:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/udp/udp_proxy/session/http_capsule/v3:pkg_cc_proto",
    ],
)
