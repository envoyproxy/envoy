load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "proxy_lib",
    srcs = [
        "proxy.cc",
    ],
    hdrs = [
        "proxy.h",
    ],
    deps = [
        ":route_lib",
        "//envoy/network:filter_interface",
        "//envoy/server:factory_context_interface",
        "//source/common/common:linked_object",
        "//source/common/common:minimal_logger_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/filters/network/common:factory_base_lib",
        "//source/extensions/filters/network/meta_protocol_proxy/interface:codec_interface",
        "//source/extensions/filters/network/meta_protocol_proxy/router:router_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/meta_protocol_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = [
        "config.cc",
    ],
    hdrs = [
        "config.h",
    ],
    deps = [
        ":proxy_lib",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/network/meta_protocol_proxy/router:config",
        "@envoy_api//envoy/extensions/filters/network/meta_protocol_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "route_lib",
    srcs = [
        "route.cc",
    ],
    hdrs = [
        "route.h",
    ],
    deps = [
        ":match_lib",
        "//envoy/server:factory_context_interface",
        "//source/common/common:matchers_lib",
        "//source/common/config:metadata_lib",
        "//source/common/config:utility_lib",
        "//source/common/matcher:matcher_lib",
        "//source/extensions/filters/network/meta_protocol_proxy/interface:config_interface",
        "//source/extensions/filters/network/meta_protocol_proxy/interface:route_interface",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/meta_protocol_proxy/action/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/meta_protocol_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "match_lib",
    srcs = [
        "match.cc",
    ],
    hdrs = [
        "match.h",
    ],
    deps = [
        "//source/common/matcher:matcher_lib",
        "//source/extensions/filters/network/meta_protocol_proxy/interface:stream_interface",
        "@envoy_api//envoy/extensions/filters/network/meta_protocol_proxy/matcher/v3:pkg_cc_proto",
    ],
)
