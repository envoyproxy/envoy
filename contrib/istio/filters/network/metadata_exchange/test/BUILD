load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "metadata_exchange_test",
    srcs = [
        "metadata_exchange_test.cc",
    ],
    repository = "@envoy",
    deps = [
        "//contrib/istio/filters/network/metadata_exchange/source:config",
        "//source/common/protobuf",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/server:server_factory_context_mocks",
    ],
)
