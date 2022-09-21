load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "router_lib",
    srcs = [
        "router.cc",
    ],
    hdrs = [
        "router.h",
    ],
    deps = [
        "//contrib/generic_proxy/filters/network/source/interface:codec_interface",
        "//contrib/generic_proxy/filters/network/source/interface:config_interface",
        "//contrib/generic_proxy/filters/network/source/interface:filter_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:linked_object",
        "//source/common/common:minimal_logger_lib",
        "//source/common/upstream:load_balancer_lib",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = [
        "config.cc",
    ],
    hdrs = [
        "config.h",
    ],
    deps = [
        ":router_lib",
        "//contrib/generic_proxy/filters/network/source/interface:config_interface",
        "//envoy/registry",
        "@envoy_api//contrib/envoy/extensions/filters/network/generic_proxy/router/v3:pkg_cc_proto",
    ],
)
