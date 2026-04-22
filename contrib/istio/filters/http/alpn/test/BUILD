load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "alpn_test",
    srcs = [
        "alpn_test.cc",
    ],
    repository = "@envoy",
    deps = [
        "//contrib/istio/filters/http/alpn/source:alpn_filter",
        "//contrib/istio/filters/http/alpn/source:config_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = [
        "config_test.cc",
    ],
    repository = "@envoy",
    deps = [
        "//contrib/istio/filters/http/alpn/source:config_lib",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
    ],
)
