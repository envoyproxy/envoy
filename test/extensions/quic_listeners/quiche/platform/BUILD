licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_platform_dep",
    "envoy_cc_test",
    "envoy_cc_test_binary",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
    "envoy_select_quiche",
)

envoy_package()

envoy_cc_test(
    name = "http2_platform_test",
    srcs = envoy_select_quiche(["http2_platform_test.cc"]),
    external_deps = ["quiche_http2_platform"],
    deps = [
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "quic_platform_test",
    srcs = envoy_select_quiche(["quic_platform_test.cc"]),
    data = [
        "//test/extensions/transport_sockets/tls/test_data:certs",
    ],
    external_deps = ["quiche_quic_platform"],
    deps = [
        "//source/common/memory:stats_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/extensions/transport_sockets/tls:ssl_test_utils",
        "//test/mocks/api:api_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "//test/test_common:utility_lib",
        "@com_googlesource_quiche//:quic_platform_port_utils",
        "@com_googlesource_quiche//:quic_platform_sleep",
    ] + envoy_cc_platform_dep("//source/exe:platform_impl_lib"),
)

envoy_cc_test(
    name = "spdy_platform_test",
    srcs = envoy_select_quiche(["spdy_platform_test.cc"]),
    external_deps = ["quiche_spdy_platform"],
    deps = [
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "quic_platform_port_utils_impl_lib",
    srcs = ["quic_port_utils_impl.cc"],
    hdrs = ["quic_port_utils_impl.h"],
    deps = [
        "//source/common/network:utility_lib",
        "//test/test_common:environment_lib",
    ],
)
