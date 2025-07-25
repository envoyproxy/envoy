load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "proxy_filter_lib",
    srcs = ["proxy_filter.cc"],
    hdrs = ["proxy_filter.h"],
    deps = [
        "//envoy/router:string_accessor_interface",
        "//source/common/common:assert_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/stream_info:uint32_accessor_lib",
        "//source/extensions/common/dynamic_forward_proxy:dns_cache_interface",
        "@envoy_api//envoy/extensions/filters/udp/udp_proxy/session/dynamic_forward_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":proxy_filter_lib",
        "//source/extensions/common/dynamic_forward_proxy:dns_cache_manager_impl",
        "//source/extensions/filters/udp/udp_proxy/session_filters:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/udp/udp_proxy/session/dynamic_forward_proxy/v3:pkg_cc_proto",
    ],
)
