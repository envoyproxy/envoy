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
    extension_names = ["envoy.http.early_header_mutation.header_mutation"],
    rbe_pool = "2core",
    deps = [
        "//source/common/formatter:formatter_extension_lib",
        "//source/extensions/http/early_header_mutation/header_mutation:header_mutation_lib",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = [
        "config_test.cc",
    ],
    extension_names = ["envoy.http.early_header_mutation.header_mutation"],
    rbe_pool = "2core",
    deps = [
        "//envoy/registry",
        "//source/common/config:utility_lib",
        "//source/common/formatter:formatter_extension_lib",
        "//source/extensions/http/early_header_mutation/header_mutation:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "header_mutation_integration_test",
    size = "large",
    srcs = [
        "header_mutation_integration_test.cc",
    ],
    extension_names = ["envoy.http.early_header_mutation.header_mutation"],
    rbe_pool = "2core",
    deps = [
        "//source/common/formatter:formatter_extension_lib",
        "//source/extensions/http/early_header_mutation/header_mutation:config",
        "//test/integration:http_integration_lib",
        "//test/integration:http_protocol_integration_lib",
        "@envoy_api//envoy/extensions/http/early_header_mutation/header_mutation/v3:pkg_cc_proto",
    ],
)
