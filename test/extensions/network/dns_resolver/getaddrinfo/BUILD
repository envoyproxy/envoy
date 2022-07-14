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
    name = "getaddrinfo_test",
    srcs = ["getaddrinfo_test.cc"],
    extension_names = ["envoy.network.dns_resolver.getaddrinfo"],
    deps = [
        "//source/extensions/network/dns_resolver/getaddrinfo:config",
        "//test/mocks/api:api_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "@envoy_api//envoy/extensions/network/dns_resolver/getaddrinfo/v3:pkg_cc_proto",
    ],
)
