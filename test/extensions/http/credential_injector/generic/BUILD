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
    name = "credential_injector_integration_test",
    size = "large",
    srcs = ["credential_injector_integration_test.cc"],
    extension_names = ["envoy.filters.http.credential_injector"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/http/credential_injector:config",
        "//source/extensions/http/injected_credentials/generic:config",
        "//test/integration:http_protocol_integration_lib",
        "//test/test_common:utility_lib",
    ],
)
