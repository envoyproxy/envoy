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
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//envoy/router:string_accessor_interface",
        "//envoy/stream_info:uint32_accessor_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/stream_info:uint32_accessor_lib",
        "//source/common/tcp_proxy",
        "//source/extensions/common/dynamic_forward_proxy:dns_cache_interface",
        "@envoy_api//envoy/extensions/filters/network/sni_dynamic_forward_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":proxy_filter_lib",
        "//source/extensions/common/dynamic_forward_proxy:dns_cache_manager_impl",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/network/sni_dynamic_forward_proxy/v3:pkg_cc_proto",
    ],
)
