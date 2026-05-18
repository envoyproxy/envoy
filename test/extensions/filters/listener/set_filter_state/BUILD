load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "set_filter_state_test",
    srcs = ["set_filter_state_test.cc"],
    deps = [
        "//source/common/network:listener_filter_buffer_lib",
        "//source/extensions/filters/listener/set_filter_state:config",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/listener/set_filter_state/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    srcs = [
        "integration_test.cc",
    ],
    extension_names = ["envoy.filters.listener.set_filter_state"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/router:string_accessor_lib",
        "//source/extensions/filters/listener/set_filter_state:config",
        "//source/extensions/filters/network/echo:config",
        "//source/extensions/formatter/metadata:config",
        "//test/integration:integration_lib",
        "//test/integration:utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)
