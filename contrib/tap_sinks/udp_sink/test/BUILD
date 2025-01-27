load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "udp_sink_test",
    srcs = ["udp_sink_test.cc"],
    deps = [
        "//contrib/tap_sinks/udp_sink/source:config",
        "//source/common/network:udp_packet_writer_handler_lib",
        "//source/common/network:utility_lib",
        "//test/mocks/network:network_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/tap/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/tap/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "udp_sink_config_test",
    srcs = ["udp_sink_config_test.cc"],
    deps = [
        "//contrib/tap_sinks/udp_sink/source:config",
        "//source/common/network:utility_lib",
        "//source/extensions/common/tap:tap_config_base",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:registry_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/tap/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/tap/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "udp_sink_interation_test",
    size = "large",
    srcs = ["udp_sink_interation_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//contrib/tap_sinks/udp_sink/source:config",
        "//source/extensions/filters/http/tap:config",
        "//test/extensions/common/tap:common",
        "//test/integration:http_integration_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/tap/v3:pkg_cc_proto",
    ],
)
