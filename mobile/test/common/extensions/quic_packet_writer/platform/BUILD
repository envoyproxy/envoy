load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_mobile_package")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_test(
    name = "platform_packet_writer_factory_test",
    srcs = [
        "platform_packet_writer_factory_test.cc",
    ],
    repository = "@envoy",
    deps = [
        "//library/common/extensions/quic_packet_writer/platform:platform_packet_writer_factory",
        "@envoy//source/common/network:udp_packet_writer_handler_lib",
        "@envoy//source/common/quic:envoy_quic_packet_writer_lib",
        "@envoy//source/common/quic:envoy_quic_utils_lib",
        "@envoy//test/mocks/api:api_mocks",
        "@envoy//test/mocks/event:event_mocks",
        "@envoy//test/test_common:threadsafe_singleton_injector_lib",
        "@envoy//test/test_common:utility_lib",
    ],
)
