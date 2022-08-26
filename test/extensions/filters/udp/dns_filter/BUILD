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
    extension_names = ["envoy.filters.udp.dns_filter"],
    external_deps = ["ares"],
    deps = [
        "//source/common/common:random_generator_lib",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//source/extensions/filters/udp/dns_filter:dns_filter_lib",
        "//test/test_common:environment_lib",
    ],
)

envoy_extension_cc_test(
    name = "dns_filter_test",
    srcs = ["dns_filter_test.cc"],
    args = [
        # Force creation of c-ares DnsResolverImpl when running test on macOS.
        "--runtime-feature-disable-for-tests=envoy.restart_features.use_apple_api_for_dns_lookups",
    ],
    extension_names = ["envoy.filters.udp.dns_filter"],
    deps = [
        ":dns_filter_test_lib",
        "//source/extensions/filters/udp/dns_filter:dns_filter_lib",
        "//source/extensions/network/dns_resolver/cares:config",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/server:listener_factory_context_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:registry_lib",
        "@envoy_api//envoy/extensions/filters/udp/dns_filter/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "dns_filter_integration_test",
    srcs = ["dns_filter_integration_test.cc"],
    extension_names = ["envoy.filters.udp.dns_filter"],
    deps = [
        ":dns_filter_test_lib",
        "//source/extensions/filters/udp/dns_filter:config",
        "//source/extensions/filters/udp/dns_filter:dns_filter_lib",
        "//test/integration:integration_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "dns_filter_utils_test",
    srcs = ["dns_filter_utils_test.cc"],
    extension_names = ["envoy.filters.udp.dns_filter"],
    deps = [
        ":dns_filter_test_lib",
        "//source/extensions/filters/udp/dns_filter:config",
        "//source/extensions/filters/udp/dns_filter:dns_filter_lib",
        "//test/integration:integration_lib",
        "@envoy_api//envoy/extensions/filters/udp/dns_filter/v3:pkg_cc_proto",
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
