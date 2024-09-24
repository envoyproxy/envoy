load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_mock(
    name = "mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//contrib/sip_proxy/filters/network/source:conn_manager_lib",
        "//contrib/sip_proxy/filters/network/source:conn_state_lib",
        "//contrib/sip_proxy/filters/network/source:utility_interface",
        "//contrib/sip_proxy/filters/network/source/filters:factory_base_lib",
        "//contrib/sip_proxy/filters/network/source/filters:filter_interface",
        "//contrib/sip_proxy/filters/network/source/router:router_interface",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:printers_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/sip_proxy/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "utility_lib",
    hdrs = ["utility.h"],
    deps = [
        "//contrib/sip_proxy/filters/network/source:sip_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:byte_order_lib",
        "//test/common/buffer:utility_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/sip_proxy/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "app_exception_impl_test",
    srcs = ["app_exception_impl_test.cc"],
    deps = [
        "//contrib/sip_proxy/filters/network/source:app_exception_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":mocks",
        "//contrib/sip_proxy/filters/network/source:config",
        "//contrib/sip_proxy/filters/network/source/router:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:registry_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/sip_proxy/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "conn_manager_test",
    srcs = ["conn_manager_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":mocks",
        ":utility_lib",
        "//contrib/sip_proxy/filters/network/source:config",
        "//contrib/sip_proxy/filters/network/source:conn_manager_lib",
        "//contrib/sip_proxy/filters/network/source:decoder_lib",
        "//contrib/sip_proxy/filters/network/source/filters:filter_interface",
        "//contrib/sip_proxy/filters/network/source/router:config",
        "//contrib/sip_proxy/filters/network/source/router:router_interface",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:printers_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/sip_proxy/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "tra_test",
    srcs = ["tra_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":mocks",
        ":utility_lib",
        "//contrib/sip_proxy/filters/network/source:config",
        "//contrib/sip_proxy/filters/network/source:conn_manager_lib",
        "//contrib/sip_proxy/filters/network/source/filters:filter_interface",
        "//contrib/sip_proxy/filters/network/source/router:config",
        "//contrib/sip_proxy/filters/network/source/router:router_interface",
        "//contrib/sip_proxy/filters/network/source/tra:tra_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:printers_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/sip_proxy/tra/v3alpha:pkg_cc_proto",
        "@envoy_api//contrib/envoy/extensions/filters/network/sip_proxy/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "decoder_test",
    srcs = ["decoder_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":mocks",
        ":utility_lib",
        "//contrib/sip_proxy/filters/network/source:app_exception_lib",
        "//contrib/sip_proxy/filters/network/source:config",
        "//contrib/sip_proxy/filters/network/source:conn_manager_lib",
        "//contrib/sip_proxy/filters/network/source:decoder_lib",
        "//contrib/sip_proxy/filters/network/source/filters:filter_interface",
        "//contrib/sip_proxy/filters/network/source/router:config",
        "//contrib/sip_proxy/filters/network/source/router:router_interface",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "router_test",
    srcs = ["router_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":mocks",
        ":utility_lib",
        "//contrib/sip_proxy/filters/network/source:app_exception_lib",
        "//contrib/sip_proxy/filters/network/source:config",
        "//contrib/sip_proxy/filters/network/source/router:config",
        "//contrib/sip_proxy/filters/network/source/router:router_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/test_common:printers_lib",
        "//test/test_common:registry_lib",
    ],
)

envoy_cc_test(
    name = "cache_manager_test",
    srcs = ["cache_manager_test.cc"],
    deps = [
        "//contrib/sip_proxy/filters/network/source:utility_interface",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)
