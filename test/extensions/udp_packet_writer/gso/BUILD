load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
    "envoy_select_enable_http3",
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
    extension_names = ["envoy.udp_packet_writer.gso"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//test/mocks/server:listener_factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/udp_packet_writer/v3:pkg_cc_proto",
    ] + envoy_select_enable_http3([
        "//source/extensions/udp_packet_writer/gso:config",
    ]),
)
