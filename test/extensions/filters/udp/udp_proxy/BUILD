load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_mock",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_mock(
    name = "mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    extension_names = ["envoy.filters.udp_listener.udp_proxy"],
    deps = [
        "//source/extensions/filters/udp/udp_proxy:udp_proxy_filter_lib",
        "//source/extensions/filters/udp/udp_proxy/session_filters:filter_interface",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:printers_lib",
    ],
)

envoy_extension_cc_test(
    name = "udp_proxy_filter_test",
    srcs = ["udp_proxy_filter_test.cc"],
    extension_names = ["envoy.filters.udp_listener.udp_proxy"],
    deps = [
        ":mocks",
        "//source/common/common:hash_lib",
        "//source/common/router:string_accessor_lib",
        "//source/common/stream_info:uint32_accessor_lib",
        "//source/extensions/access_loggers/file:config",
        "//source/extensions/filters/udp/udp_proxy:config",
        "//source/extensions/filters/udp/udp_proxy:udp_proxy_filter_lib",
        "//source/extensions/matching/network/common:inputs_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/http:stream_encoder_mock",
        "//test/mocks/network:socket_mocks",
        "//test/mocks/server:listener_factory_context_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/mocks/upstream:cluster_update_callbacks_handle_mocks",
        "//test/mocks/upstream:cluster_update_callbacks_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/mocks/upstream:thread_local_cluster_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "@envoy_api//envoy/config/accesslog/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/file/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/udp/udp_proxy/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "hash_policy_impl_test",
    srcs = ["hash_policy_impl_test.cc"],
    extension_names = ["envoy.filters.udp_listener.udp_proxy"],
    deps = [
        "//source/common/common:hash_lib",
        "//source/common/network:address_lib",
        "//source/extensions/filters/udp/udp_proxy:hash_policy_lib",
        "@envoy_api//envoy/extensions/filters/udp/udp_proxy/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "router_impl_test",
    srcs = ["router_impl_test.cc"],
    extension_names = ["envoy.filters.udp_listener.udp_proxy"],
    deps = [
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//source/extensions/filters/udp/udp_proxy/router:router_lib",
        "//source/extensions/matching/network/common:inputs_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/udp/udp_proxy/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "udp_proxy_integration_test",
    size = "large",
    srcs = ["udp_proxy_integration_test.cc"],
    extension_names = ["envoy.filters.udp_listener.udp_proxy"],
    deps = [
        "//envoy/network:filter_interface",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/udp/udp_proxy:config",
        "//source/extensions/filters/udp/udp_proxy/session_filters:filter_config_interface",
        "//source/extensions/filters/udp/udp_proxy/session_filters:filter_interface",
        "//test/extensions/filters/udp/udp_proxy/session_filters:buffer_filter_config_lib",
        "//test/extensions/filters/udp/udp_proxy/session_filters:buffer_filter_proto_cc_proto",
        "//test/extensions/filters/udp/udp_proxy/session_filters:drainer_filter_config_lib",
        "//test/extensions/filters/udp/udp_proxy/session_filters:drainer_filter_proto_cc_proto",
        "//test/integration:integration_lib",
        "//test/test_common:registry_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)
