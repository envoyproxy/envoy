load(
    "//bazel:envoy_build_system.bzl",
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
    extension_names = ["envoy.udp_packet_writer.default"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/extensions/udp_packet_writer/default:config",
        "//test/mocks/server:listener_factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/udp_packet_writer/v3:pkg_cc_proto",
    ],
)
