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
    name = "header_mutation_test",
    srcs = [
        "header_mutation_test.cc",
    ],
    extension_names = ["envoy.filters.http.header_mutation"],
    rbe_pool = "2core",
    deps = [
        "//source/common/formatter:formatter_extension_lib",
        "//source/extensions/filters/http/header_mutation:config",
        "//test/mocks/api:api_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.header_mutation"],
    rbe_pool = "2core",
    deps = [
        "//source/common/formatter:formatter_extension_lib",
        "//source/extensions/filters/http/header_mutation:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "header_mutation_integration_test",
    size = "large",
    srcs = ["header_mutation_integration_test.cc"],
    extension_names = ["envoy.filters.http.header_mutation"],
    rbe_pool = "2core",
    deps = [
        "//source/common/formatter:formatter_extension_lib",
        "//source/extensions/filters/http/header_mutation:config",
        "//test/integration:http_integration_lib",
        "//test/mocks/server:instance_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@com_google_absl//absl/strings:str_format",
    ],
)
