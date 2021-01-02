load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package(enabled_default = False)

envoy_cc_library(
    name = "tcp_post_filter_lib",
    srcs = ["tcp_post_filter.cc"],
    hdrs = ["tcp_post_filter.h"],
    deps = [
        "//include/envoy/http:filter_interface",
        "//include/envoy/http:header_map_interface",
        "//source/common/http:header_map_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/tcp_post/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        "//include/envoy/registry",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/tcp_post:tcp_post_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/tcp_post/v3:pkg_cc_proto",
    ],
)
