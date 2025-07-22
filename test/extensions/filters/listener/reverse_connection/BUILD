load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "reverse_connection_test",
    srcs = ["reverse_connection_test.cc"],
    extension_names = ["envoy.filters.listener.reverse_connection"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/network:listener_filter_buffer_lib",
        "//source/common/network:utility_lib",
        "//source/extensions/bootstrap/reverse_tunnel:reverse_connection_utility_lib",
        "//source/extensions/filters/listener/reverse_connection:config_lib",
        "//source/extensions/filters/listener/reverse_connection:reverse_connection_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:network_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/listener/reverse_connection/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "reverse_connection_config_test",
    srcs = ["reverse_connection_config_test.cc"],
    extension_names = ["envoy.filters.listener.reverse_connection"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/listener/reverse_connection:config_factory_lib",
        "//source/extensions/filters/listener/reverse_connection:config_lib",
        "//source/extensions/filters/listener/reverse_connection:reverse_connection_lib",
        "//test/mocks/server:listener_factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/listener/reverse_connection/v3:pkg_cc_proto",
    ],
) 