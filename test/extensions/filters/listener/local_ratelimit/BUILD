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
    name = "local_ratelimit_test",
    srcs = ["local_ratelimit_test.cc"],
    extension_names = ["envoy.filters.listener.local_ratelimit"],
    deps = [
        "//source/common/network:default_socket_interface_lib",
        "//source/common/network:listener_filter_buffer_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/listener/local_ratelimit:config",
        "//source/extensions/filters/listener/local_ratelimit:local_ratelimit_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stats:stats_mocks",
    ],
)
