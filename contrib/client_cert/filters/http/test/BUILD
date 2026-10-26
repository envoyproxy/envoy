load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "client_cert_filter_test",
    srcs = ["client_cert_filter_test.cc"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//contrib/client_cert/filters/http/source:client_cert_filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//contrib/client_cert/filters/http/source:config",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "integration_test",
    srcs = ["integration_test.cc"],
    data = [
        "//test/config/integration/certs",
    ],
    deps = [
        "//contrib/client_cert/filters/http/source:config",
        "//source/common/tls:context_lib",
        "//test/integration:http_integration_lib",
        "//test/integration:utility_lib",
        "//test/test_common:utility_lib",
    ],
)
