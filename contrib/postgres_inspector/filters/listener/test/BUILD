load("@rules_cc//cc:cc_library.bzl", "cc_library")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "postgres_inspector_test",
    srcs = ["postgres_inspector_test.cc"],
    repository = "@envoy",
    deps = [
        ":postgres_test_utils_lib",
        "//contrib/postgres_inspector/filters/listener/source:postgres_inspector_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//contrib/envoy/extensions/filters/listener/postgres_inspector/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "postgres_inspector_integration_test",
    srcs = ["postgres_inspector_integration_test.cc"],
    repository = "@envoy",
    deps = [
        ":postgres_test_utils_lib",
        "//contrib/postgres_inspector/filters/listener/source:config",
        "//source/common/network:utility_lib",
        "//source/extensions/filters/listener/tls_inspector:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/integration:integration_lib",
        "//test/integration:utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "postgres_message_parser_test",
    srcs = [
        "postgres_message_parser_test.cc",
    ],
    repository = "@envoy",
    deps = [
        ":postgres_test_utils_lib",
        "//contrib/postgres_inspector/filters/listener/source:postgres_message_parser_lib",
        "//test/test_common:utility_lib",
    ],
)

cc_library(
    name = "postgres_test_utils_lib",
    srcs = [],
    hdrs = ["postgres_test_utils.h"],
    deps = [
        "//contrib/postgres/protocol:postgres_protocol_lib",
        "//source/common/buffer:buffer_lib",
    ],
)

envoy_cc_test(
    name = "postgres_inspector_sni_integration_test",
    srcs = ["postgres_inspector_sni_integration_test.cc"],
    repository = "@envoy",
    deps = [
        ":postgres_test_utils_lib",
        "//contrib/postgres_inspector/filters/listener/source:config",
        "//contrib/postgres_proxy/filters/network/source:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/integration:integration_lib",
        "//test/integration:utility_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/postgres_proxy/v3alpha:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
    ],
)
