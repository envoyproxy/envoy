load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "router_lib",
    srcs = [
        "router.cc",
    ],
    hdrs = [
        "router.h",
    ],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:linked_object",
        "//source/common/common:minimal_logger_lib",
        "//source/common/upstream:load_balancer_lib",
        "//source/extensions/filters/network/meta_protocol_proxy/interface:codec_interface",
        "//source/extensions/filters/network/meta_protocol_proxy/interface:config_interface",
        "//source/extensions/filters/network/meta_protocol_proxy/interface:filter_interface",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = [
        "config.cc",
    ],
    hdrs = [
        "config.h",
    ],
    deps = [
        ":router_lib",
        "//envoy/registry",
        "//source/extensions/filters/network/meta_protocol_proxy/interface:config_interface",
        "@envoy_api//envoy/extensions/filters/network/meta_protocol_proxy/router/v3:pkg_cc_proto",
    ],
)
