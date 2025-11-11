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
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.upstream.local_address_selector.linux_network_namespace"],
    deps = [
        "//source/common/network:address_lib",
        "//source/common/network:transport_socket_options_lib",
        "//source/common/stream_info:filter_state_lib",
        "//source/extensions/local_address_selectors/linux_network_namespace:config",
    ],
)
