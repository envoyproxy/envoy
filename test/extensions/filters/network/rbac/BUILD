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
    extension_name = "envoy.filters.network.rbac",
    deps = [
        "//source/extensions/filters/network/rbac:config",
        "//test/mocks/server:server_mocks",
        "@envoy_api//envoy/config/rbac/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/rbac/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "filter_test",
    srcs = ["filter_test.cc"],
    extension_name = "envoy.filters.network.rbac",
    deps = [
        "//source/extensions/filters/common/rbac:utility_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/rbac:rbac_filter",
        "//test/mocks/network:network_mocks",
        "@envoy_api//envoy/config/rbac/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/rbac/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    srcs = ["integration_test.cc"],
    extension_name = "envoy.filters.network.rbac",
    deps = [
        "//source/extensions/filters/network/echo:config",
        "//source/extensions/filters/network/rbac:config",
        "//test/integration:integration_lib",
        "//test/test_common:environment_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/rbac/v3:pkg_cc_proto",
    ],
)
