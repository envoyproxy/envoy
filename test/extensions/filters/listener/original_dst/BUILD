load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_fuzz_test(
    name = "original_dst_fuzz_test",
    srcs = ["original_dst_fuzz_test.cc"],
    corpus = "original_dst_corpus",
    deps = [
        "//source/extensions/filters/listener/original_dst:original_dst_lib",
        "//test/extensions/filters/listener/common/fuzz:listener_filter_fuzzer_lib",
    ],
)

envoy_cc_test(
    name = "original_dst_integration_test",
    size = "large",
    srcs = [
        "original_dst_integration_test.cc",
    ],
    deps = [
        "//source/common/http:header_map_lib",
        "//source/extensions/clusters/original_dst:original_dst_cluster_lib",
        "//source/extensions/filters/http/buffer:config",
        "//source/extensions/filters/listener/original_dst:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/integration:http_protocol_integration_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/listener/original_dst/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "original_dst_test",
    srcs = ["original_dst_test.cc"],
    deps = [
        "//source/common/network:filter_state_dst_address_lib",
        "//source/common/network:listener_filter_buffer_lib",
        "//source/common/network:utility_lib",
        "//source/extensions/filters/listener/original_dst:original_dst_lib",
        "//test/mocks/network:network_mocks",
        "//test/test_common:utility_lib",
    ],
)
