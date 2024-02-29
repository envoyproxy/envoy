load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":payload_to_metadata_filter_lib",
        "//envoy/registry",
        "//source/extensions/filters/network/thrift_proxy/filters:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/network/thrift_proxy/filters/payload_to_metadata/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "payload_to_metadata_filter_lib",
    srcs = ["payload_to_metadata_filter.cc"],
    hdrs = ["payload_to_metadata_filter.h"],
    deps = [
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/network/thrift_proxy:auto_protocol_lib",
        "//source/extensions/filters/network/thrift_proxy:auto_transport_lib",
        "//source/extensions/filters/network/thrift_proxy:decoder_lib",
        "//source/extensions/filters/network/thrift_proxy/filters:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/network/thrift_proxy/filters/payload_to_metadata/v3:pkg_cc_proto",
    ],
)
