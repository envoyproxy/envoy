load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_proto_library(
    name = "dfp_setter_filter_proto",
    srcs = ["dfp_setter.proto"],
)

envoy_cc_test_library(
    name = "dfp_setter_filter_config_lib",
    srcs = ["dfp_setter.h"],
    deps = [
        ":dfp_setter_filter_proto_cc_proto",
        "//envoy/registry",
        "//source/common/router:string_accessor_lib",
        "//source/common/stream_info:uint32_accessor_lib",
        "//source/extensions/filters/udp/udp_proxy/session_filters:factory_base_lib",
        "//source/extensions/filters/udp/udp_proxy/session_filters:filter_interface",
        "//test/test_common:utility_lib",
    ],
    alwayslink = 1,
)

envoy_extension_cc_test(
    name = "proxy_filter_test",
    srcs = ["proxy_filter_test.cc"],
    extension_names = ["envoy.filters.udp.session.dynamic_forward_proxy"],
    deps = [
        "//source/common/router:string_accessor_lib",
        "//source/common/stream_info:uint32_accessor_lib",
        "//source/extensions/filters/udp/udp_proxy/session_filters/dynamic_forward_proxy:config",
        "//test/extensions/common/dynamic_forward_proxy:mocks",
        "//test/extensions/filters/udp/udp_proxy:mocks",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//envoy/extensions/filters/udp/udp_proxy/session/dynamic_forward_proxy/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "dynamic_forward_proxy_filter_integration_test",
    srcs = ["proxy_filter_integration_test.cc"],
    extension_names = ["envoy.filters.udp.session.dynamic_forward_proxy"],
    tags = ["cpu:3"],
    deps = [
        ":dfp_setter_filter_config_lib",
        ":dfp_setter_filter_proto_cc_proto",
        "//envoy/network:filter_interface",
        "//envoy/server:filter_config_interface",
        "//source/extensions/clusters/dynamic_forward_proxy:cluster",
        "//source/extensions/filters/udp/udp_proxy:config",
        "//source/extensions/filters/udp/udp_proxy/session_filters/dynamic_forward_proxy:config",
        "//source/extensions/filters/udp/udp_proxy/session_filters/dynamic_forward_proxy:proxy_filter_lib",
        "//test/integration:integration_lib",
        "//test/test_common:registry_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)
