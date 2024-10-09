load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
    "envoy_proto_library",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "filter_test",
    srcs = ["filter_test.cc"],
    extension_names = ["envoy.filters.http.credential_injector"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/http/credential_injector:credential_injector_lib",
        "//source/extensions/http/injected_credentials/generic:generic_lib",
        "//source/extensions/http/injected_credentials/oauth2:client_credentials_lib",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_proto_library(
    name = "mock_credentail",
    srcs = ["mock_credential.proto"],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.credential_injector"],
    rbe_pool = "2core",
    deps = [
        ":mock_credentail_cc_proto",
        "//source/extensions/filters/http/credential_injector:config",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
    ],
)
