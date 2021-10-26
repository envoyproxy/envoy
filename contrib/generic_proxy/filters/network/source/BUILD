load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "filter_lib",
    srcs = [
        "generic_proxy.cc",
    ],
    hdrs = [
        "generic_proxy.h",
    ],
    repository = "@envoy",
    deps = [
        ":route_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/generic_proxy/v3:pkg_cc_proto",
        "//contrib/generic_proxy/filters/network/source/interface:generic_codec_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/server:factory_context_interface",
        "//source/common/common:linked_object",
        "//source/common/common:minimal_logger_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/filters/network/common:factory_base_lib",
    ],
)

envoy_cc_library(
    name = "factory_lib",
    srcs = [
        "config.cc",
    ],
    hdrs = [
        "config.h",
    ],
    repository = "@envoy",
    deps = [
        ":filter_lib",
        "//include/envoy/server:filter_config_interface",
    ],
)

envoy_cc_library(
    name = "route_lib",
    srcs = [
        "route_impl.cc",
    ],
    hdrs = [
        "route_impl.h",
    ],
    repository = "@envoy",
    deps = [
        "//api/proxy/filters/network/generic_proxy/v2:pkg_cc_proto",
        "//contrib/generic_proxy/filters/network/source/interface:generic_config_interface",
        "//contrib/generic_proxy/filters/network/source/interface:generic_route_interface",
        "//include/envoy/server:factory_context_interface",
        "//source/common/common:matchers_lib",
        "//source/common/config:metadata_lib",
        "//source/common/config:utility_lib",
    ],
)
