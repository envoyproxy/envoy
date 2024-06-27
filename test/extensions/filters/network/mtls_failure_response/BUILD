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
    extension_names = ["envoy.filters.network.mtls_failure_response"],
    deps = [
        "//source/extensions/filters/network/mtls_failure_response:config",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//envoy/extensions/filters/network/mtls_failure_response/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "mtls_failure_response_integration_test",
    srcs = ["mtls_failure_response_integration_test.cc"],
    extension_names = ["envoy.filters.network.mtls_failure_response"],
    deps = [
        "//source/extensions/filters/network/mtls_failure_response:config",
        "//source/extensions/filters/network/mtls_failure_response:filter",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//envoy/extensions/filters/network/mtls_failure_response/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)
