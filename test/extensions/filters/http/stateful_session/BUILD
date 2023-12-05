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
    name = "stateful_session_test",
    srcs = [
        "stateful_session_test.cc",
    ],
    extension_names = ["envoy.filters.http.stateful_session"],
    deps = [
        "//source/extensions/filters/http/stateful_session:config",
        "//source/server:generic_factory_context_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/http:stateful_session_mock",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "stateful_session_integration_test",
    size = "large",
    srcs = [
        "stateful_session_integration_test.cc",
    ],
    extension_names = ["envoy.filters.http.stateful_session"],
    deps = [
        "//source/common/protobuf",
        "//source/extensions/filters/http/stateful_session:config",
        "//source/extensions/http/stateful_session/cookie:config",
        "//source/extensions/http/stateful_session/header:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.stateful_session"],
    deps = [
        "//source/extensions/filters/http/stateful_session:config",
        "//test/mocks/http:stateful_session_mock",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)
