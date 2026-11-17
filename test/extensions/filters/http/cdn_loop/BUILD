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
    extension_names = ["envoy.filters.http.cdn_loop"],
    rbe_pool = {
        "default": {"build": "linux_x64_small"},
        "coverage": {
            "build": "linux_x64_small",
            "link": "linux_x64_large",
        },
        "tsan": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
        },
    },
    deps = [
        "//source/extensions/filters/http/cdn_loop:config",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/cdn_loop/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "filter_integration_test",
    size = "large",
    srcs = ["filter_integration_test.cc"],
    extension_names = ["envoy.filters.http.cdn_loop"],
    # linux_x64_xlarge: the coverage link peaks at 15154MB,
    # over linux_x64_large (12288MiB) with headroom.
    rbe_pool = {
        "default": {"build": "linux_x64_small"},
        "cto": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
        },
        "openssl": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
        },
        "coverage": {
            "build": "linux_x64_small",
            "link": "linux_x64_xlarge",
        },
        "tsan": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
        },
        "asan": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
        },
    },
    deps = [
        "//source/extensions/filters/http/cdn_loop:config",
        "//test/integration:http_protocol_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/cdn_loop/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "filter_test",
    srcs = ["filter_test.cc"],
    rbe_pool = {
        "gcc": {"build": "linux_x64_small"},
        "coverage": {"link": "linux_x64_large"},
        "tsan": {"link": "linux_x64_small"},
    },
    deps = [
        "//envoy/http:codes_interface",
        "//envoy/http:filter_interface",
        "//source/extensions/filters/http/cdn_loop:filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "parser_test",
    srcs = ["parser_test.cc"],
    rbe_pool = {"coverage": {"link": "linux_x64_small"}},
    deps = [
        "//source/extensions/filters/http/cdn_loop:parser_lib",
        "//test/test_common:status_utility_lib",
    ],
)

envoy_cc_fuzz_test(
    name = "parser_fuzz_test",
    srcs = ["parser_fuzz_test.cc"],
    corpus = "parser_corpus",
    # linux_x64_xlarge: the fuzz coverage link peaks at 11512MB,
    # over linux_x64_large (12288MiB) with headroom.
    rbe_pool = {
        "tsan": {"link": "linux_x64_small"},
        "fuzz_coverage": {"link": "linux_x64_xlarge"},
    },
    deps = [
        "//source/common/common:statusor_lib",
        "//source/extensions/filters/http/cdn_loop:parser_lib",
        "//test/fuzz:utility_lib",
    ],
)

envoy_cc_test(
    name = "utils_test",
    srcs = ["utils_test.cc"],
    rbe_pool = {"coverage": {"link": "linux_x64_small"}},
    deps = [
        "//source/extensions/filters/http/cdn_loop:utils_lib",
        "//test/test_common:status_utility_lib",
    ],
)
