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
    name = "regex_mutation_test",
    srcs = [
        "regex_mutation_test.cc",
    ],
    extension_names = ["envoy.http.early_header_mutation.regex_mutation"],
    deps = [
        "//source/extensions/http/early_header_mutation/regex_mutation:regex_mutation_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = [
        "config_test.cc",
    ],
    extension_names = ["envoy.http.early_header_mutation.regex_mutation"],
    deps = [
        "//envoy/registry",
        "//source/common/config:utility_lib",
        "//source/extensions/http/early_header_mutation/regex_mutation:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "regex_mutation_integration_test",
    srcs = [
        "regex_mutation_integration_test.cc",
    ],
    extension_names = ["envoy.http.early_header_mutation.regex_mutation"],
    deps = [
        "//source/extensions/http/early_header_mutation/regex_mutation:config",
        "//test/integration:http_integration_lib",
        "//test/integration:http_protocol_integration_lib",
        "@envoy_api//envoy/extensions/http/early_header_mutation/regex_mutation/v3:pkg_cc_proto",
    ],
)
