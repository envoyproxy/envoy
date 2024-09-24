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
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.config.validators.minimum_clusters_validator"],
    rbe_pool = "2core",
    deps = [
        "//envoy/registry",
        "//source/common/protobuf:message_validator_lib",
        "//source/extensions/config/validators/minimum_clusters:config",
        "@envoy_api//envoy/extensions/config/validators/minimum_clusters/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "minimum_clusters_validator_test",
    srcs = ["minimum_clusters_validator_test.cc"],
    extension_names = ["envoy.config.validators.minimum_clusters_validator"],
    rbe_pool = "2core",
    deps = [
        "//source/common/config:decoded_resource_lib",
        "//source/extensions/config/validators/minimum_clusters:minimum_clusters_validator",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
    ],
)

envoy_extension_cc_test(
    name = "minimum_clusters_validator_integration_test",
    size = "large",
    srcs = ["minimum_clusters_validator_integration_test.cc"],
    extension_names = ["envoy.config.validators.minimum_clusters_validator"],
    rbe_pool = "2core",
    deps = [
        "//source/common/config:protobuf_link_hacks",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/config/validators/minimum_clusters:config",
        "//source/extensions/config/validators/minimum_clusters:minimum_clusters_validator",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/config:v2_link_hacks",
        "//test/integration:http_integration_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:network_utility_lib",
        "//test/test_common:resources_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/config/validators/minimum_clusters/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)
