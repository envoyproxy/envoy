load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_cc_test_library",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_mock(
    name = "mocks_lib",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/network/dubbo_proxy:decoder_events_lib",
        "//source/extensions/filters/network/dubbo_proxy:decoder_lib",
        "//source/extensions/filters/network/dubbo_proxy:protocol_interface",
        "//source/extensions/filters/network/dubbo_proxy:serializer_interface",
        "//source/extensions/filters/network/dubbo_proxy/filters:factory_base_lib",
        "//source/extensions/filters/network/dubbo_proxy/filters:filter_interface",
        "//source/extensions/filters/network/dubbo_proxy/router:router_interface",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:printers_lib",
    ],
)

envoy_cc_test_library(
    name = "utility_lib",
    hdrs = ["utility.h"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:byte_order_lib",
    ],
)

envoy_extension_cc_test(
    name = "hessian_utils_test",
    srcs = ["hessian_utils_test.cc"],
    extension_name = "envoy.filters.network.dubbo_proxy",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:byte_order_lib",
        "//source/extensions/filters/network/dubbo_proxy:hessian_utils_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "dubbo_protocol_impl_test",
    srcs = ["dubbo_protocol_impl_test.cc"],
    extension_name = "envoy.filters.network.dubbo_proxy",
    deps = [
        ":mocks_lib",
        ":utility_lib",
        "//source/extensions/filters/network/dubbo_proxy:dubbo_protocol_impl_lib",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_extension_cc_test(
    name = "dubbo_hessian2_serializer_impl_test",
    srcs = ["dubbo_hessian2_serializer_impl_test.cc"],
    extension_name = "envoy.filters.network.dubbo_proxy",
    deps = [
        ":mocks_lib",
        ":utility_lib",
        "//source/extensions/filters/network/dubbo_proxy:dubbo_hessian2_serializer_impl_lib",
        "//source/extensions/filters/network/dubbo_proxy:hessian_utils_lib",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.network.dubbo_proxy",
    deps = [
        ":mocks_lib",
        ":utility_lib",
        "//source/extensions/filters/network/dubbo_proxy:config",
        "//source/extensions/filters/network/dubbo_proxy/filters:filter_config_interface",
        "//test/mocks/server:server_mocks",
        "//test/test_common:registry_lib",
        "@envoy_api//envoy/extensions/filters/network/dubbo_proxy/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "metadata_test",
    srcs = ["metadata_test.cc"],
    extension_name = "envoy.filters.network.dubbo_proxy",
    deps = [
        "//source/extensions/filters/network/dubbo_proxy:metadata_lib",
        "//source/extensions/filters/network/dubbo_proxy:serializer_interface",
    ],
)

envoy_extension_cc_test(
    name = "route_matcher_test",
    srcs = ["route_matcher_test.cc"],
    extension_name = "envoy.filters.network.dubbo_proxy",
    deps = [
        "//source/extensions/filters/network/dubbo_proxy:metadata_lib",
        "//source/extensions/filters/network/dubbo_proxy/router:route_matcher",
        "//test/mocks/server:server_mocks",
        "@envoy_api//envoy/extensions/filters/network/dubbo_proxy/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "router_test",
    srcs = ["router_test.cc"],
    extension_name = "envoy.filters.network.dubbo_proxy",
    deps = [
        ":mocks_lib",
        "//source/extensions/filters/network/dubbo_proxy:app_exception_lib",
        "//source/extensions/filters/network/dubbo_proxy:dubbo_hessian2_serializer_impl_lib",
        "//source/extensions/filters/network/dubbo_proxy:dubbo_protocol_impl_lib",
        "//source/extensions/filters/network/dubbo_proxy:metadata_lib",
        "//source/extensions/filters/network/dubbo_proxy/router:config",
        "//test/mocks/server:server_mocks",
        "//test/test_common:registry_lib",
    ],
)

envoy_extension_cc_test(
    name = "app_exception_test",
    srcs = ["app_exception_test.cc"],
    extension_name = "envoy.filters.network.dubbo_proxy",
    deps = [
        ":mocks_lib",
        ":utility_lib",
        "//source/extensions/filters/network/dubbo_proxy:app_exception_lib",
        "//source/extensions/filters/network/dubbo_proxy:dubbo_hessian2_serializer_impl_lib",
        "//source/extensions/filters/network/dubbo_proxy:dubbo_protocol_impl_lib",
        "//source/extensions/filters/network/dubbo_proxy:hessian_utils_lib",
        "//source/extensions/filters/network/dubbo_proxy:metadata_lib",
    ],
)

envoy_extension_cc_test(
    name = "router_filter_config_test",
    srcs = ["router_filter_config_test.cc"],
    extension_name = "envoy.filters.network.dubbo_proxy",
    deps = [
        ":mocks_lib",
        "//source/extensions/filters/network/dubbo_proxy/filters:well_known_names",
        "//source/extensions/filters/network/dubbo_proxy/router:config",
        "//test/mocks/server:server_mocks",
        "@envoy_api//envoy/extensions/filters/network/dubbo_proxy/router/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "decoder_test",
    srcs = ["decoder_test.cc"],
    extension_name = "envoy.filters.network.dubbo_proxy",
    deps = [
        ":mocks_lib",
        ":utility_lib",
        "//source/extensions/filters/network/dubbo_proxy:decoder_lib",
        "//source/extensions/filters/network/dubbo_proxy:metadata_lib",
    ],
)

envoy_extension_cc_test(
    name = "conn_manager_test",
    srcs = ["conn_manager_test.cc"],
    extension_name = "envoy.filters.network.dubbo_proxy",
    deps = [
        ":mocks_lib",
        ":utility_lib",
        "//source/extensions/filters/network/dubbo_proxy:config",
        "//source/extensions/filters/network/dubbo_proxy:conn_manager_lib",
        "//source/extensions/filters/network/dubbo_proxy:dubbo_hessian2_serializer_impl_lib",
        "//source/extensions/filters/network/dubbo_proxy:dubbo_protocol_impl_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/mocks/server:server_mocks",
        "@envoy_api//envoy/extensions/filters/network/dubbo_proxy/v3:pkg_cc_proto",
    ],
)
