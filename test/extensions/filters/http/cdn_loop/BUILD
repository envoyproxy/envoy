load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
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
    extension_name = "envoy.filters.http.cdn_loop",
    deps = [
        "//source/extensions/filters/http/cdn_loop:config",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/cdn_loop/v3alpha:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "filter_integration_test",
    srcs = ["filter_integration_test.cc"],
    extension_name = "envoy.filters.http.cdn_loop",
    deps = [
        "//source/extensions/filters/http/cdn_loop:config",
        "//test/integration:http_protocol_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/cdn_loop/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "filter_test",
    srcs = ["filter_test.cc"],
    deps = [
        "//include/envoy/http:codes_interface",
        "//include/envoy/http:filter_interface",
        "//source/extensions/filters/http/cdn_loop:filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "parser_test",
    srcs = ["parser_test.cc"],
    deps = [
        "//source/extensions/filters/http/cdn_loop:parser_lib",
        "//test/test_common:status_utility_lib",
    ],
)

envoy_cc_fuzz_test(
    name = "parser_fuzz_test",
    srcs = ["parser_fuzz_test.cc"],
    corpus = "parser_corpus",
    deps = [
        "//source/common/common:statusor_lib",
        "//source/extensions/filters/http/cdn_loop:parser_lib",
        "//test/fuzz:utility_lib",
    ],
)

envoy_cc_test(
    name = "utils_test",
    srcs = ["utils_test.cc"],
    deps = [
        "//source/extensions/filters/http/cdn_loop:utils_lib",
        "//test/test_common:status_utility_lib",
    ],
)
