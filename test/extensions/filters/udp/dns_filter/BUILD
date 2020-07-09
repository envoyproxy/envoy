load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
    "envoy_extension_cc_test_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test_library(
    name = "dns_filter_test_lib",
    srcs = ["dns_filter_test_utils.cc"],
    hdrs = ["dns_filter_test_utils.h"],
    extension_name = "envoy.filters.udp_listener.dns_filter",
    deps = [
        "//source/extensions/filters/udp/dns_filter:dns_filter_lib",
        "//test/test_common:environment_lib",
    ],
)

envoy_extension_cc_test(
    name = "dns_filter_test",
    srcs = ["dns_filter_test.cc"],
    extension_name = "envoy.filters.udp_listener.dns_filter",
    tags = ["fails_on_windows"],
    deps = [
        ":dns_filter_test_lib",
        "//source/extensions/filters/udp/dns_filter:dns_filter_lib",
        "//test/mocks/server:server_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:environment_lib",
        "@envoy_api//envoy/extensions/filters/udp/dns_filter/v3alpha:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "dns_filter_integration_test",
    srcs = ["dns_filter_integration_test.cc"],
    extension_name = "envoy.filters.udp_listener.dns_filter",
    tags = ["fails_on_windows"],
    deps = [
        ":dns_filter_test_lib",
        "//source/extensions/filters/udp/dns_filter:config",
        "//source/extensions/filters/udp/dns_filter:dns_filter_lib",
        "//test/integration:integration_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_cc_fuzz_test(
    name = "dns_filter_fuzz_test",
    srcs = ["dns_filter_fuzz_test.cc"],
    corpus = "dns_filter_corpus",
    deps = [
        "//source/extensions/filters/udp/dns_filter:dns_filter_lib",
        "//test/fuzz:utility_lib",
        "//test/test_common:environment_lib",
    ],
)
