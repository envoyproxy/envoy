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
    size = "small",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.upstream.local_address_selector.default_local_address_selector"],
    deps = [
        "//envoy/common:base_includes",
        "//envoy/network:socket_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//source/extensions/upstream/local_address_selector:config",
        "//test/test_common:utility_lib",
    ],
)
