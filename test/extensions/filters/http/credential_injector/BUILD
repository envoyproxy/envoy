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
    deps = [
        ":mock_credentail_cc_proto",
        "//source/extensions/filters/http/credential_injector:config",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "credential_injector_integration_test",
    size = "large",
    srcs = ["credential_injector_integration_test.cc"],
    extension_names = ["envoy.filters.http.credential_injector"],
    deps = [
        "//source/extensions/filters/http/credential_injector:config",
        "//source/extensions/http/injected_credentials/generic:config",
        "//test/integration:http_protocol_integration_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "oauth2_config_test",
    srcs = ["oauth2_config_test.cc"],
    extension_names = ["envoy.filters.http.credential_injector"],
    deps = [
        "//source/extensions/http/injected_credentials/oauth2:config",
        "//test/mocks/server:factory_context_mocks",
    ],
)

envoy_extension_cc_test(
    name = "token_provider_test",
    srcs = ["token_provider_test.cc"],
    extension_names = ["envoy.filters.http.credential_injector"],
    deps = [
        "//source/extensions/http/injected_credentials/common:secret_reader_lib",
        "//source/extensions/http/injected_credentials/oauth2:token_provider_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
    ],
)

envoy_extension_cc_test(
    name = "credential_injector_oauth_integration_test",
    srcs = ["credential_injector_oauth_integration_test.cc"],
    extension_names = ["envoy.filters.http.credential_injector"],
    deps = [
        "//source/extensions/filters/http/credential_injector:config",
        "//source/extensions/http/injected_credentials/oauth2:config",
        "//source/extensions/http/injected_credentials/oauth2:oauth_response_cc_proto",
        "//test/integration:http_protocol_integration_lib",
        "//test/test_common:utility_lib",
    ],
)
