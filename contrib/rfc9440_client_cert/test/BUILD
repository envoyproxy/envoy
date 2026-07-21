load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "filter_test",
    srcs = ["filter_test.cc"],
    deps = [
        "//contrib/rfc9440_client_cert/source:rfc9440_client_cert_filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/ssl:ssl_mocks",
    ],
)
