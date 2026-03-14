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
    extension_names = ["envoy.bootstrap.certificate_providers.local"],
    deps = [
        "//envoy/common:exception_lib",
        "//envoy/registry",
        "//source/extensions/bootstrap/certificate_providers/local:config",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/bootstrap/certificate_providers/local/v3:pkg_cc_proto",
    ],
)
