load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_extension_package",
)

licenses(["notice"])

envoy_extension_package()

envoy_cc_test(
    name = "filter_test",
    srcs = ["filter_test.cc"],
    deps = [
        "//source/extensions/filters/http/rfc9440_client_cert:rfc9440_client_cert_filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/ssl:ssl_mocks",
    ],
)
