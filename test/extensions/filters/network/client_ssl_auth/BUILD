licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "client_ssl_auth_test",
    srcs = ["client_ssl_auth_test.cc"],
    data = glob(["test_data/**"]),
    deps = [
        "//source/common/config:filter_json_lib",
        "//source/extensions/filters/network/client_ssl_auth",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    deps = [
        "//source/extensions/filters/network/client_ssl_auth:config",
        "//test/mocks/server:server_mocks",
    ],
)
