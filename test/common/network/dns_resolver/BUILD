load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "dns_factory_test",
    srcs = ["dns_factory_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/network/dns_resolver:dns_factory_util_lib",
        "//source/extensions/network/dns_resolver/cares:config",
        "//test/mocks/network:network_mocks",
    ],
)
