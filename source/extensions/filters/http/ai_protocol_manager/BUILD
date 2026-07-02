load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "filter_chain_bridge_lib",
    srcs = ["filter_chain_bridge.cc"],
    hdrs = ["filter_chain_bridge.h"],
    deps = [
        "//envoy/http:codes_interface",
        "//envoy/http:filter_interface",
        "//source/common/external_buffer:buffer_manager_lib",
    ],
)

envoy_cc_library(
    name = "filter_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        ":filter_chain_bridge_lib",
        "//source/common/common:logger_lib",
        "//source/common/external_buffer:buffer_manager_lib",
        "//source/common/external_buffer:external_buffer_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":filter_lib",
        "//envoy/registry",
        "//source/common/external_buffer:external_buffer_impl_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/ai_protocol_manager/v3:pkg_cc_proto",
    ],
)
