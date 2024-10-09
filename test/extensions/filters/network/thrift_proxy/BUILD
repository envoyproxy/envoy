load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_mock",
    "envoy_extension_cc_test",
    "envoy_extension_cc_test_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_mock(
    name = "mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    extension_names = ["envoy.filters.network.thrift_proxy"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/network/thrift_proxy:conn_manager_lib",
        "//source/extensions/filters/network/thrift_proxy:protocol_interface",
        "//source/extensions/filters/network/thrift_proxy:transport_interface",
        "//source/extensions/filters/network/thrift_proxy/filters:factory_base_lib",
        "//source/extensions/filters/network/thrift_proxy/filters:filter_interface",
        "//source/extensions/filters/network/thrift_proxy/router:router_interface",
        "//source/extensions/filters/network/thrift_proxy/router:router_ratelimit_interface",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:printers_lib",
    ],
)

envoy_extension_cc_test_library(
    name = "integration_lib",
    srcs = ["integration.cc"],
    hdrs = ["integration.h"],
    extension_names = ["envoy.filters.network.thrift_proxy"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/network/thrift_proxy:config",
        "//source/extensions/filters/network/thrift_proxy:conn_manager_lib",
        "//source/extensions/filters/network/thrift_proxy/router:config",
        "//test/integration:integration_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
    ],
)

envoy_extension_cc_test_library(
    name = "utility_lib",
    hdrs = ["utility.h"],
    extension_names = ["envoy.filters.network.thrift_proxy"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:byte_order_lib",
        "//source/extensions/filters/network/thrift_proxy:thrift_lib",
        "//test/common/buffer:utility_lib",
        "@envoy_api//envoy/extensions/filters/network/thrift_proxy/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "app_exception_impl_test",
    srcs = ["app_exception_impl_test.cc"],
    extension_names = ["envoy.filters.network.thrift_proxy"],
    rbe_pool = "2core",
    deps = [
        ":mocks",
        "//source/extensions/filters/network/thrift_proxy:app_exception_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "buffer_helper_test",
    srcs = ["buffer_helper_test.cc"],
    extension_names = ["envoy.filters.network.thrift_proxy"],
    rbe_pool = "2core",
    deps = [
        ":utility_lib",
        "//source/extensions/filters/network/thrift_proxy:buffer_helper_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "conn_state_test",
    srcs = ["conn_state_test.cc"],
    extension_names = ["envoy.filters.network.thrift_proxy"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/network/thrift_proxy:conn_state_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "auto_transport_impl_test",
    srcs = ["auto_transport_impl_test.cc"],
    extension_names = ["envoy.filters.network.thrift_proxy"],
    rbe_pool = "2core",
    deps = [
        ":mocks",
        ":utility_lib",
        "//source/extensions/filters/network/thrift_proxy:auto_transport_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "framed_transport_impl_test",
    srcs = ["framed_transport_impl_test.cc"],
    extension_names = ["envoy.filters.network.thrift_proxy"],
    rbe_pool = "2core",
    deps = [
        ":mocks",
        ":utility_lib",
        "//source/extensions/filters/network/thrift_proxy:framed_transport_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "header_transport_impl_test",
    srcs = ["header_transport_impl_test.cc"],
    extension_names = ["envoy.filters.network.thrift_proxy"],
    rbe_pool = "2core",
    deps = [
        ":mocks",
        ":utility_lib",
        "//source/extensions/filters/network/thrift_proxy:header_transport_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "unframed_transport_impl_test",
    srcs = ["unframed_transport_impl_test.cc"],
    extension_names = ["envoy.filters.network.thrift_proxy"],
    rbe_pool = "2core",
    deps = [
        ":mocks",
        ":utility_lib",
        "//source/extensions/filters/network/thrift_proxy:unframed_transport_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "auto_protocol_impl_test",
    srcs = ["auto_protocol_impl_test.cc"],
    extension_names = ["envoy.filters.network.thrift_proxy"],
    rbe_pool = "2core",
    deps = [
        ":mocks",
        ":utility_lib",
        "//source/extensions/filters/network/thrift_proxy:auto_protocol_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "binary_protocol_impl_test",
    srcs = ["binary_protocol_impl_test.cc"],
    extension_names = ["envoy.filters.network.thrift_proxy"],
    rbe_pool = "2core",
    deps = [
        ":mocks",
        ":utility_lib",
        "//source/extensions/filters/network/thrift_proxy:binary_protocol_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "compact_protocol_impl_test",
    srcs = ["compact_protocol_impl_test.cc"],
    extension_names = ["envoy.filters.network.thrift_proxy"],
    rbe_pool = "2core",
    deps = [
        ":mocks",
        ":utility_lib",
        "//source/extensions/filters/network/thrift_proxy:compact_protocol_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "twitter_protocol_impl_test",
    srcs = ["twitter_protocol_impl_test.cc"],
    extension_names = ["envoy.filters.network.thrift_proxy"],
    rbe_pool = "2core",
    deps = [
        ":mocks",
        ":utility_lib",
        "//source/extensions/filters/network/thrift_proxy:framed_transport_lib",
        "//source/extensions/filters/network/thrift_proxy:twitter_protocol_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.network.thrift_proxy"],
    rbe_pool = "2core",
    deps = [
        ":mocks",
        "//source/extensions/filters/network/thrift_proxy:config",
        "//source/extensions/filters/network/thrift_proxy/router:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:registry_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/thrift_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "conn_manager_test",
    srcs = ["conn_manager_test.cc"],
    extension_names = ["envoy.filters.network.thrift_proxy"],
    rbe_pool = "2core",
    deps = [
        ":mocks",
        ":utility_lib",
        "//source/common/formatter:formatter_extension_lib",
        "//source/extensions/access_loggers/file:config",
        "//source/extensions/filters/network/thrift_proxy:config",
        "//source/extensions/filters/network/thrift_proxy:conn_manager_lib",
        "//source/extensions/filters/network/thrift_proxy/filters:filter_interface",
        "//source/extensions/filters/network/thrift_proxy/router:config",
        "//source/extensions/filters/network/thrift_proxy/router:router_interface",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:printers_lib",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/extensions/filters/network/thrift_proxy/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "decoder_test",
    srcs = ["decoder_test.cc"],
    extension_names = ["envoy.filters.network.thrift_proxy"],
    rbe_pool = "2core",
    deps = [
        ":mocks",
        ":utility_lib",
        "//source/extensions/filters/network/thrift_proxy:app_exception_lib",
        "//source/extensions/filters/network/thrift_proxy:decoder_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "metadata_test",
    srcs = ["metadata_test.cc"],
    extension_names = ["envoy.filters.network.thrift_proxy"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/network/thrift_proxy:metadata_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "router_test",
    srcs = ["router_test.cc"],
    extension_names = ["envoy.filters.network.thrift_proxy"],
    rbe_pool = "2core",
    deps = [
        ":mocks",
        ":utility_lib",
        "//source/extensions/filters/network/thrift_proxy:app_exception_lib",
        "//source/extensions/filters/network/thrift_proxy:config",
        "//source/extensions/filters/network/thrift_proxy/router:config",
        "//source/extensions/filters/network/thrift_proxy/router:router_lib",
        "//source/extensions/filters/network/thrift_proxy/router:shadow_writer_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/test_common:printers_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/thrift/router/v2alpha1:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "router_ratelimit_test",
    srcs = ["router_ratelimit_test.cc"],
    extension_names = ["envoy.filters.network.thrift_proxy"],
    rbe_pool = "2core",
    deps = [
        ":mocks",
        ":utility_lib",
        "//envoy/common:base_includes",
        "//source/common/network:address_lib",
        "//source/extensions/filters/network/thrift_proxy:config",
        "//source/extensions/filters/network/thrift_proxy/router:config",
        "//source/extensions/filters/network/thrift_proxy/router:router_ratelimit_lib",
        "//test/mocks/ratelimit:ratelimit_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:printers_lib",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/thrift_proxy/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "route_matcher_test",
    srcs = ["route_matcher_test.cc"],
    extension_names = ["envoy.filters.network.thrift_proxy"],
    rbe_pool = "2core",
    deps = [
        ":utility_lib",
        "//source/extensions/filters/network/thrift_proxy/router:config",
        "//source/extensions/filters/network/thrift_proxy/router:router_interface",
        "//source/extensions/filters/network/thrift_proxy/router:router_lib",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/filter/thrift/router/v2alpha1:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/thrift_proxy/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "thrift_object_impl_test",
    srcs = ["thrift_object_impl_test.cc"],
    extension_names = ["envoy.filters.network.thrift_proxy"],
    rbe_pool = "2core",
    deps = [
        ":mocks",
        ":utility_lib",
        "//source/extensions/filters/network/thrift_proxy:thrift_object_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:registry_lib",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    size = "large",
    srcs = ["integration_test.cc"],
    data = [
        "//test/extensions/filters/network/thrift_proxy/driver:generate_fixture",
    ],
    extension_names = ["envoy.filters.network.thrift_proxy"],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        ":integration_lib",
        ":utility_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:printers_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "translation_integration_test",
    size = "large",
    srcs = ["translation_integration_test.cc"],
    data = [
        "//test/extensions/filters/network/thrift_proxy/driver:generate_fixture",
    ],
    extension_names = ["envoy.filters.network.thrift_proxy"],
    rbe_pool = "2core",
    deps = [
        ":integration_lib",
        ":utility_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:printers_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/thrift_proxy/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "shadow_writer_test",
    srcs = ["shadow_writer_test.cc"],
    extension_names = ["envoy.filters.network.thrift_proxy"],
    rbe_pool = "2core",
    deps = [
        ":mocks",
        ":utility_lib",
        "//source/extensions/filters/network/thrift_proxy:app_exception_lib",
        "//source/extensions/filters/network/thrift_proxy:config",
        "//source/extensions/filters/network/thrift_proxy/router:shadow_writer_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/test_common:printers_lib",
        "//test/test_common:registry_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/thrift_proxy/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "trds_integration_test",
    size = "large",
    srcs = ["trds_integration_test.cc"],
    data = [
        "//test/extensions/filters/network/thrift_proxy/driver:generate_fixture",
    ],
    extension_names = ["envoy.filters.network.thrift_proxy"],
    rbe_pool = "2core",
    deps = [
        ":integration_lib",
        "//test/test_common:network_utility_lib",
        "@envoy_api//envoy/extensions/filters/network/thrift_proxy/v3:pkg_cc_proto",
    ],
)
