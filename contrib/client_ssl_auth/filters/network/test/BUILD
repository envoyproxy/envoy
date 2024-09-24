load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "client_ssl_auth_test",
    srcs = ["client_ssl_auth_test.cc"],
    data = glob(["test_data/**"]),
    rbe_pool = "2core",
    deps = [
        "//contrib/client_ssl_auth/filters/network/source:client_ssl_auth",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/client_ssl_auth/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//contrib/client_ssl_auth/filters/network/source:config",
        "//source/common/protobuf:utility_lib",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//contrib/envoy/extensions/filters/network/client_ssl_auth/v3:pkg_cc_proto",
    ],
)
