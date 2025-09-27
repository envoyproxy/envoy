load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "reverse_connection_cluster_test",
    srcs = ["reverse_connection_cluster_test.cc"],
    copts = select({
        "//bazel:windows_x86_64": [],  # TODO: fix the windows ANTLR build
        "//conditions:default": [],
    }),
    tags = ["skip_on_windows"],
    deps = [
        "//envoy/registry",
        "//source/common/formatter:formatter_extension_lib",
        "//source/extensions/clusters/reverse_connection:reverse_connection_lib",
        "//source/extensions/load_balancing_policies/cluster_provided:config",
        "//test/common/upstream:utility_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:admin_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/clusters/reverse_connection/v3:pkg_cc_proto",
    ],
)
