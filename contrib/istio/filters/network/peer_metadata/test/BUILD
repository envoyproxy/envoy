load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "peer_metadata_test",
    srcs = ["peer_metadata_test.cc"],
    deps = [
        "//contrib/istio/filters/network/peer_metadata/source:config",
        "//envoy/router:string_accessor_interface",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "@abseil-cpp//absl/strings",
    ],
)
