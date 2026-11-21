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
    name = "fault_filter_test",
    srcs = ["fault_filter_test.cc"],
    extension_names = ["envoy.filters.network.fault"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/fault:fault_filter_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/extensions/filters/network/fault/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.network.fault"],
    deps = [
        "//envoy/registry",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/fault:config",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/network/fault/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "fault_filter_integration_test",
    size = "large",
    srcs = ["fault_filter_integration_test.cc"],
    data = [
        "//test/config/integration/certs",
    ],
    extension_names = ["envoy.filters.network.fault"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/extensions/access_loggers/file:config",
        "//source/extensions/filters/network/echo:config",
        "//source/extensions/filters/network/fault:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/integration:integration_lib",
        "//test/integration:utility_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/file/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/fault/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
    ],
)
